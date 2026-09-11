// SPDX-License-Identifier: WTFPL
//! Linux-only, single-threaded evdev → Sway rotation. No runtime or periodic timer.
use serde_json::Value;
use std::{
    env, fs,
    fs::{File, OpenOptions},
    io::{self, Read, Write},
    mem::{size_of, zeroed},
    os::{
        fd::{AsRawFd, FromRawFd, OwnedFd, RawFd},
        unix::{fs::OpenOptionsExt, net::UnixStream},
    },
    path::{Path, PathBuf},
    time::{Duration, Instant},
};

type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
// Linux input-event-codes.h; not exported by libc.
const EV_SYN: u16 = 0;
const EV_ABS: u16 = 3;
const SYN_REPORT: u16 = 0;
const SYN_DROPPED: u16 = 3;
const STALE: Duration = Duration::from_secs(4);
const DWELL: Duration = Duration::from_secs(1);
const SENSOR_NAME: &[u8] = b"ThinkPad EC accelerometer";

#[derive(Clone, Debug)]
struct Profile {
    center: [f64; 2],
    positive: [f64; 2],
    negative: [f64; 2],
}
impl Default for Profile {
    fn default() -> Self {
        Self {
            center: [572., 571.],
            positive: [131., 125.],
            negative: [203., 211.],
        }
    }
}
impl Profile {
    fn parse(bytes: &[u8]) -> Result<Self> {
        let value: Value = serde_json::from_slice(bytes)?;
        let pair = |key: &str, positive: bool| -> Result<[f64; 2]> {
            let a = value[key]
                .as_array()
                .filter(|a| a.len() == 2)
                .ok_or_else(|| format!("{key} must contain two numbers"))?;
            let number = |v: &Value| -> Result<f64> {
                v.as_f64()
                    .filter(|v| v.is_finite() && (!positive || *v > 0.))
                    .ok_or_else(|| format!("Invalid {key} value").into())
            };
            Ok([number(&a[0])?, number(&a[1])?])
        };
        Ok(Self {
            center: pair("center", false)?,
            positive: pair("positive", true)?,
            negative: pair("negative", true)?,
        })
    }
    fn classify(&self, axes: [i32; 2]) -> Option<&'static str> {
        let mut v = [0.; 2];
        for i in 0..2 {
            let delta = axes[i] as f64 - self.center[i];
            v[i] = delta
                / if delta >= 0. {
                    self.positive[i]
                } else {
                    self.negative[i]
                };
        }
        let [x, y] = v;
        if x.abs().max(y.abs()) < 0.25 {
            return Some("normal");
        }
        if x.abs().max(y.abs()) < 0.65 || (x.abs() - y.abs()).abs() < 0.25 {
            return None;
        }
        Some(if x.abs() > y.abs() {
            if x > 0. {
                "270"
            } else {
                "90"
            }
        } else if y > 0. {
            "normal"
        } else {
            "180"
        })
    }
}

// evdev suppresses unchanged ABS values. Retain each axis between reports;
// after an overflow ignore events until SYN_REPORT and obtain a fresh snapshot.
struct Axes {
    values: [i32; 2],
    dropped: bool,
}
impl Axes {
    fn event(&mut self, kind: u16, code: u16, value: i32) {
        if kind == EV_SYN && code == SYN_DROPPED {
            self.dropped = true;
        } else if !self.dropped && kind == EV_ABS && code < 2 {
            self.values[code as usize] = value;
        }
    }
}

struct Debounce {
    candidate: Option<&'static str>,
    since: Instant,
}
impl Debounce {
    fn new(now: Instant) -> Self {
        Self {
            candidate: None,
            since: now,
        }
    }
    fn update(&mut self, target: Option<&'static str>, now: Instant) -> Option<&'static str> {
        if target != self.candidate {
            self.candidate = target;
            self.since = now;
        }
        target.filter(|_| now.duration_since(self.since) >= DWELL)
    }
}

#[derive(Default)]
struct Args {
    device: PathBuf,
    output: Option<String>,
    profile: Option<PathBuf>,
    dry_run: bool,
}
fn arguments() -> Result<Option<Args>> {
    let mut args = Args {
        device: "/dev/input/thinkpad-ec-accel".into(),
        ..Args::default()
    };
    let mut iter = env::args_os().skip(1);
    while let Some(arg) = iter.next() {
        match arg.to_str() {
            Some("--device") => args.device = iter.next().ok_or("--device needs a path")?.into(),
            Some("--profile") => {
                args.profile = Some(iter.next().ok_or("--profile needs a path")?.into())
            }
            Some("--output") => {
                args.output = Some(
                    iter.next()
                        .ok_or("--output needs a name")?
                        .into_string()
                        .map_err(|_| "Output name must be UTF-8")?,
                )
            }
            Some("--dry-run") => args.dry_run = true,
            Some("--help" | "-h") => {
                println!("thinkpad-rotate [--device PATH] [--output NAME] [--profile JSON] [--dry-run]\n\
                    Opt-in Sway rotation from the X230 EC's raw two-axis accelerometer.\n\
                    Defaults: /dev/input/thinkpad-ec-accel, sole active eDP/LVDS display.\n\
                    Profile: center/positive/negative arrays of two numbers.\n\
                    Ctrl-C or SIGTERM closes acquisition and restores the original transform.");
                return Ok(None);
            }
            _ => return Err(format!("Unknown argument: {}", arg.to_string_lossy()).into()),
        }
    }
    Ok(Some(args))
}

// Linux signalfd avoids signal handlers and wakes poll immediately. This binary
// is single-threaded: block before starting acquisition, restore on final drop.
struct Signals {
    fd: OwnedFd,
    previous: libc::sigset_t,
}
impl Signals {
    fn new() -> Result<Self> {
        // SAFETY: initialized sigsets, valid output pointers; no other threads.
        unsafe {
            let mut set = zeroed();
            let mut previous = zeroed();
            libc::sigemptyset(&mut set);
            libc::sigaddset(&mut set, libc::SIGINT);
            libc::sigaddset(&mut set, libc::SIGTERM);
            if libc::sigprocmask(libc::SIG_BLOCK, &set, &mut previous) < 0 {
                return Err(io::Error::last_os_error().into());
            }
            let fd = libc::signalfd(-1, &set, libc::SFD_CLOEXEC | libc::SFD_NONBLOCK);
            if fd < 0 {
                let error = io::Error::last_os_error();
                libc::sigprocmask(libc::SIG_SETMASK, &previous, std::ptr::null_mut());
                return Err(error.into());
            }
            Ok(Self {
                fd: OwnedFd::from_raw_fd(fd),
                previous,
            })
        }
    }
}
impl Drop for Signals {
    fn drop(&mut self) {
        // SAFETY: previous is the mask obtained by sigprocmask in this thread.
        unsafe {
            libc::sigprocmask(libc::SIG_SETMASK, &self.previous, std::ptr::null_mut());
        }
    }
}

#[derive(Debug, PartialEq)]
enum Wake {
    Data,
    Stop,
    Timeout,
}
fn wait(sensor: RawFd, signal: RawFd, deadline: Instant) -> Result<Wake> {
    loop {
        let remaining = deadline.saturating_duration_since(Instant::now());
        // Round up to avoid a sub-millisecond busy loop at the deadline.
        let millis = remaining.as_nanos().div_ceil(1_000_000);
        let mut fds = [
            libc::pollfd {
                fd: sensor,
                events: libc::POLLIN,
                revents: 0,
            },
            libc::pollfd {
                fd: signal,
                events: libc::POLLIN,
                revents: 0,
            },
        ];
        // SAFETY: fds is a writable array of the supplied length, valid for call.
        let rc = unsafe {
            libc::poll(
                fds.as_mut_ptr(),
                fds.len() as libc::nfds_t,
                millis.min(i32::MAX as u128) as i32,
            )
        };
        if rc < 0 {
            let error = io::Error::last_os_error();
            if error.kind() == io::ErrorKind::Interrupted {
                continue;
            }
            return Err(error.into());
        }
        if fds[1].revents & libc::POLLIN != 0 {
            let mut info = [0u8; size_of::<libc::signalfd_siginfo>()];
            // SAFETY: buffer length matches its writable allocation.
            let n = unsafe { libc::read(signal, info.as_mut_ptr().cast(), info.len()) };
            if n != info.len() as isize {
                return Err("Cannot read stop signal".into());
            }
            return Ok(Wake::Stop);
        }
        if fds
            .iter()
            .any(|f| f.revents & (libc::POLLERR | libc::POLLHUP | libc::POLLNVAL) != 0)
        {
            return Err("Sensor or signal descriptor disconnected".into());
        }
        if fds[0].revents & libc::POLLIN != 0 {
            return Ok(Wake::Data);
        }
        if Instant::now() >= deadline {
            return Ok(Wake::Timeout);
        }
    }
}

struct Sway {
    path: PathBuf,
}
impl Sway {
    fn request(&self, kind: u32, body: &str) -> Result<Value> {
        let mut sock = UnixStream::connect(&self.path)?;
        sock.set_read_timeout(Some(Duration::from_secs(3)))?;
        sock.set_write_timeout(Some(Duration::from_secs(3)))?;
        ipc_request(&mut sock, kind, body)
    }
    fn transform(&self, output: &str, value: &str) -> Result<()> {
        if !matches!(
            value,
            "normal"
                | "90"
                | "180"
                | "270"
                | "flipped"
                | "flipped-90"
                | "flipped-180"
                | "flipped-270"
        ) {
            return Err("Invalid Sway transform".into());
        }
        let command = format!(
            "output {} transform {value}",
            serde_json::to_string(output)?
        );
        let reply = self.request(0, &command)?;
        if !reply
            .as_array()
            .is_some_and(|a| !a.is_empty() && a.iter().all(|r| r["success"] == true))
        {
            return Err(format!("Sway rejected transform: {reply}").into());
        }
        Ok(())
    }
}
fn ipc_request(sock: &mut UnixStream, kind: u32, body: &str) -> Result<Value> {
    sock.write_all(b"i3-ipc")?;
    sock.write_all(&(body.len() as u32).to_le_bytes())?;
    sock.write_all(&kind.to_le_bytes())?;
    sock.write_all(body.as_bytes())?;
    let mut header = [0; 14];
    sock.read_exact(&mut header)?;
    let length = u32::from_le_bytes(header[6..10].try_into()?);
    if &header[..6] != b"i3-ipc"
        || u32::from_le_bytes(header[10..14].try_into()?) != kind
        || length > 4 * 1024 * 1024
    {
        return Err("Unexpected Sway IPC response".into());
    }
    let mut data = vec![0; length as usize];
    sock.read_exact(&mut data)?;
    Ok(serde_json::from_slice(&data)?)
}
fn select_output(outputs: &Value, requested: Option<&str>) -> Result<(String, String)> {
    let matches: Vec<_> = outputs
        .as_array()
        .ok_or("Invalid Sway outputs")?
        .iter()
        .filter(|o| {
            o["active"] == true
                && o["name"].as_str().is_some_and(|name| {
                    requested.map_or_else(
                        || name.starts_with("eDP-") || name.starts_with("LVDS-"),
                        |r| r == name,
                    )
                })
        })
        .collect();
    if matches.len() != 1 {
        return Err("Expected one active internal display; use --output".into());
    }
    Ok((
        matches[0]["name"]
            .as_str()
            .ok_or("Missing output name")?
            .into(),
        matches[0]["transform"]
            .as_str()
            .ok_or("Missing output transform")?
            .into(),
    ))
}

struct Rotation<'a> {
    sway: &'a Sway,
    output: String,
    original: String,
    current: String,
    dry_run: bool,
}
impl Rotation<'_> {
    fn set(&mut self, value: &str) -> Result<()> {
        if value != self.current {
            if !self.dry_run {
                self.sway.transform(&self.output, value)?;
            }
            self.current = value.into();
            println!("{}: {value}", self.output);
        }
        Ok(())
    }
    fn restore(&mut self) -> Result<()> {
        self.set(&self.original.clone())
    }
}

fn axes(fd: RawFd) -> Result<[i32; 2]> {
    let mut result = [0; 2];
    for (axis, value) in result.iter_mut().enumerate() {
        // Linux EVIOCGABS(axis), input_absinfo has six signed 32-bit fields.
        let mut info = [0i32; 6];
        // SAFETY: ioctl writes exactly sizeof(input_absinfo) bytes into info.
        if unsafe { libc::ioctl(fd, (0x80184540 + axis) as libc::c_ulong, info.as_mut_ptr()) } < 0 {
            return Err(io::Error::last_os_error().into());
        }
        *value = info[0];
    }
    Ok(result)
}
fn check_sensor(fd: RawFd) -> Result<()> {
    let mut name = [0u8; 256];
    // SAFETY: EVIOCGNAME(256) writes at most 256 bytes into name.
    if unsafe { libc::ioctl(fd, 0x81004506 as libc::c_ulong, name.as_mut_ptr()) } < 0 {
        return Err(io::Error::last_os_error().into());
    }
    if name.split(|b| *b == 0).next() != Some(SENSOR_NAME) {
        return Err("Unexpected sensor device".into());
    }
    Ok(())
}
fn run_sensor(
    mut sensor: File,
    signals: &Signals,
    rotation: &mut Rotation<'_>,
    profile: &Profile,
) -> Result<()> {
    check_sensor(sensor.as_raw_fd())?;
    let mut cached = Axes {
        values: axes(sensor.as_raw_fd())?,
        dropped: false,
    };
    let mut last = Instant::now();
    let mut debounce = Debounce::new(last);
    let mut buffer = [0u8; size_of::<libc::input_event>() * 64];
    println!(
        "Watching {}; restore={}; dry_run={}",
        rotation.output, rotation.original, rotation.dry_run
    );
    loop {
        match wait(sensor.as_raw_fd(), signals.fd.as_raw_fd(), last + STALE)? {
            Wake::Stop => return Ok(()),
            Wake::Timeout => return Err("Sensor data stopped; closing device".into()),
            Wake::Data => {}
        }
        let length = match sensor.read(&mut buffer) {
            Err(e)
                if matches!(
                    e.kind(),
                    io::ErrorKind::WouldBlock | io::ErrorKind::Interrupted
                ) =>
            {
                continue
            }
            other => other?,
        };
        if length == 0 {
            return Err("Sensor disconnected".into());
        }
        if length % size_of::<libc::input_event>() != 0 {
            return Err("Truncated input event".into());
        }
        for bytes in buffer[..length].chunks_exact(size_of::<libc::input_event>()) {
            // SAFETY: full-sized byte slice; input_event contains only integer
            // fields, all bit patterns valid. Buffer alignment is not assumed.
            let event =
                unsafe { std::ptr::read_unaligned(bytes.as_ptr().cast::<libc::input_event>()) };
            cached.event(event.type_, event.code, event.value);
            if event.type_ == EV_SYN && event.code == SYN_DROPPED {
                debounce = Debounce::new(Instant::now());
            }
            if event.type_ == EV_SYN && event.code == SYN_REPORT {
                let now = Instant::now();
                if now.duration_since(last) > Duration::from_secs(3) {
                    debounce = Debounce::new(now);
                }
                last = now;
                if cached.dropped {
                    cached.values = axes(sensor.as_raw_fd())?;
                    cached.dropped = false;
                }
                if let Some(target) = debounce.update(profile.classify(cached.values), now) {
                    rotation.set(target)?;
                }
            }
        }
    }
}
fn sway_socket(runtime: &Path) -> Result<PathBuf> {
    if let Some(path) = env::var_os("SWAYSOCK") {
        if !path.is_empty() {
            return Ok(path.into());
        }
    }
    let paths: Vec<_> = fs::read_dir(runtime)?
        .filter_map(|e| e.ok())
        .filter(|e| {
            let name = e.file_name();
            let name = name.to_string_lossy();
            name.starts_with("sway-ipc.") && name.ends_with(".sock")
        })
        .map(|e| e.path())
        .collect();
    if paths.len() != 1 {
        return Err("Expected one Sway socket; set SWAYSOCK explicitly".into());
    }
    Ok(paths[0].clone())
}
fn run() -> Result<()> {
    let Some(args) = arguments()? else {
        return Ok(());
    };
    let profile = match args.profile {
        Some(p) => Profile::parse(&fs::read(p)?)?,
        None => Profile::default(),
    };
    // SAFETY: getuid takes no arguments and has no failure result.
    let runtime = env::var_os("XDG_RUNTIME_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(format!("/run/user/{}", unsafe { libc::getuid() })));
    let _lock = OpenOptions::new()
        .write(true)
        .create(true)
        .truncate(false)
        .mode(0o600)
        .custom_flags(libc::O_NOFOLLOW | libc::O_CLOEXEC)
        .open(runtime.join("thinkpad-rotate.lock"))?;
    // SAFETY: flock operates on a live fd; held until _lock drops at exit.
    if unsafe { libc::flock(_lock.as_raw_fd(), libc::LOCK_EX | libc::LOCK_NB) } < 0 {
        return Err(format!(
            "Cannot lock rotation session: {}",
            io::Error::last_os_error()
        )
        .into());
    }
    let signals = Signals::new()?;
    let sway = Sway {
        path: sway_socket(&runtime)?,
    };
    let (output, original) = select_output(&sway.request(3, "")?, args.output.as_deref())?;
    let mut rotation = Rotation {
        sway: &sway,
        output,
        current: original.clone(),
        original,
        dry_run: args.dry_run,
    };
    let sensor = OpenOptions::new()
        .read(true)
        .custom_flags(libc::O_NONBLOCK | libc::O_CLOEXEC)
        .open(args.device)?;
    // run_sensor owns the fd: release acquisition before restoration IPC, even
    // on sensor/IPC failure. Attempt restoration while termination is blocked.
    let result = run_sensor(sensor, &signals, &mut rotation, &profile);
    let restored = rotation.restore();
    if let Err(error) = &restored {
        eprintln!("thinkpad-rotate: restore failed: {error}");
    }
    result.and(restored)
}
fn main() {
    if let Err(error) = run() {
        eprintln!("thinkpad-rotate: {error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests;
