/* SPDX-License-Identifier: WTFPL */
/* Fixed G2HT35WW battery cache reads. No target-memory write operation. */
#include <linux/delay.h>
static bool cell_voltages;
module_param(cell_voltages, bool, 0400);
MODULE_PARM_DESC(cell_voltages, "Enable experimental G2HT35WW SANYO cell-group cache reads");
static unsigned long cells_sampled;
static bool cells_attempted;
static int cells_error;
static long cells_mv[3];
static int read32(u32 addr, u32 *value)
{
 u8 v, bytes[4];
 int i, ret;
 ret=ec_write(0x91,addr>>16);
 if(!ret)ret=ec_write(0x92,addr>>8);
 if(!ret)ret=ec_write(0x93,addr);
 for(i=0;!ret&&i<4;i++)ret=ec_write(0x94+i,0xa5);
 if(!ret)ret=ec_write(0x90,0x95); /* busy | read32 */
 if(ret)return ret;
 for(i=0;i<100;i++) {
  ret=ec_read(0x90,&v);
  if(ret)return ret;
  if(!(v&0x80))break;
  msleep(2);
 }
 if(i==100)return -ETIMEDOUT;
 for(i=0;i<4;i++) {
  ret=ec_read(0x94+i,&bytes[i]);
  if(ret)return ret;
 }
 *value=((u32)bytes[0]<<24)|((u32)bytes[1]<<16)|((u32)bytes[2]<<8)|bytes[3];
 if(*value==0xa5a5a5a5)return -EACCES;
 return 0;
}
static int unlock_debug(void)
{
 static const u8 key[8] = { 0x1,0x1e,0x12,0x10,0xe0,0x78,0xd8,0x89 };
 u8 saved[8], cmd;
 int i, ret;
 ret=ec_read(0x3d,&cmd);
 if(ret || cmd&0x80)return ret?ret:-EBUSY;
 for(i=0;i<8;i++){ret=ec_read(0x3e + i,&saved[i]);if(ret)return ret;}
 for(i=0;i<8;i++){ret=ec_write(0x3e + i,key[i]);if(ret)goto restore;}
 ret=ec_write(0x3d,0xc1);
 if(ret)goto restore;
 for(i=0;i<100;i++){
  ret=ec_read(0x3d,&cmd);
  if(ret || !(cmd&0x80))break;
  msleep(2);
 }
 if(i==100)return -ETIMEDOUT;
restore:
 for(i=0;i<8;i++){int r=ec_write(0x3e + i,saved[i]);if(r)ret=r;}
 return ret;
}

/* cache_lock held by caller. This path never changes EC81 or charger policy. */
static int cells_snapshot(void)
{
 static const u32 addresses[] = {
  0x800c24, 0x800c74, 0x800c78, 0x800c84, 0x800c88,
  0x800c8c, 0x800c50, 0x800ca8, 0x800cac, 0x800c24
 };
 u32 words[ARRAY_SIZE(addresses)];
 u8 saved[7], busy, name[12], maker[8];
 int ret, restore, i;
 long values[3], sum, pack;
 /* Model fallback and experimental override never authorize unknown RAM layouts. */
 if (!cell_voltages || !known_ec_layout) return -ENODATA;
 if (ACPI_FAILURE(acpi_acquire_mutex(NULL, EC_LOCK, 2000))) return -EBUSY;
 ret = ec_read(0x90, &busy);
 if (ret || busy & 0x80) { ret = ret ? ret : -EBUSY; goto unlock; }
 for (i=0;i<7;i++) { ret=ec_read(0x91+i,&saved[i]); if(ret)goto unlock; }
 ret=read32(addresses[0],&words[0]);
 if(ret == -EACCES) {
  ret=unlock_debug();
  if(!ret)ret=read32(addresses[0],&words[0]);
 }
 for(i=1;!ret && i<ARRAY_SIZE(addresses);i++) ret=read32(addresses[i],&words[i]);
 /* A timeout means the command may still be using the request buffer. */
 if(ret != -ETIMEDOUT) {
  restore=ec_write(0x90,0);
  for(i=0;i<7;i++) { int r=ec_write(0x91+i,saved[i]); if(r)restore=r; }
  if(restore)ret=restore;
 }
 if(ret)goto unlock;
 for(i=0;i<8;i++)maker[i]=words[1+i/4] >> (8*(i%4));
 for(i=0;i<12;i++)name[i]=words[3+i/4] >> (8*(i%4));
 if(!(words[0]&4) || !(words[9]&4) ||
    memcmp(maker,"SANYO",6) || memcmp(name,"LNV-45N1023",12)) {
  ret=-ENODATA;goto unlock;
 }
 pack=words[6]&0xffff;
 values[0]=words[7]>>16; values[1]=words[8]&0xffff; values[2]=words[8]>>16;
 sum=values[0]+values[1]+values[2];
 for(i=0;i<3;i++) if(values[i]<2000 || values[i]>4500) { ret=-ENODATA;goto unlock; }
 /* Different EC cache update times are possible; reject implausible sums. */
 if(abs(sum-pack)>100) { ret=-ENODATA;goto unlock; }
 memcpy(cells_mv,values,sizeof(values));
unlock:
 acpi_release_mutex(NULL,EC_LOCK);
 return ret;
}
static int read_cell(int channel,long *value)
{
 int ret;
 mutex_lock(&cache_lock);
 if(!cells_attempted || time_after_eq(jiffies,cells_sampled+10*HZ)) {
  cells_error=cells_snapshot();cells_sampled=jiffies;cells_attempted=true;
 }
 ret=cells_error;
 if(!ret)*value=cells_mv[channel];
 mutex_unlock(&cache_lock);
 return ret;
}
