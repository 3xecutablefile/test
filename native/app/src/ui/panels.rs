use sysinfo::{CpuExt, System, SystemExt};
use std::time::{Duration, Instant};

pub struct Panels {
    sys: System,
    last_sample: Instant,
    pub cpu_use: f32, // 0..1
    pub mem_use: f32, // 0..1
    // simple smoothing state
    v_cpu: f32,
    v_mem: f32,
    target_dt: Duration,
}

impl Panels {
    pub fn new() -> Self {
        let mut sys = System::new();
        sys.refresh_memory();
        sys.refresh_cpu();
        Self {
            sys,
            last_sample: Instant::now() - Duration::from_secs(1),
            cpu_use: 0.0,
            mem_use: 0.0,
            v_cpu: 0.0,
            v_mem: 0.0,
            target_dt: Duration::from_millis(350), // throttle cadence
        }
    }

    // Call from AboutToWait or a dedicated non-render tick — cheap (~0.1–0.4ms typical)
    pub fn tick(&mut self) {
        if self.last_sample.elapsed() < self.target_dt {
            return;
        }
        self.last_sample = Instant::now();

        self.sys.refresh_memory();
        self.sys.refresh_cpu();

        // CPU: average across all CPUs
        let avg = if self.sys.cpus().is_empty() {
            0.0
        } else {
            self.sys.cpus().iter().map(|c| c.cpu_usage() as f32).sum::<f32>()
                / self.sys.cpus().len() as f32
        };
        let mem = self.sys.used_memory() as f32 / self.sys.total_memory().max(1) as f32;

        // smooth towards target (simple critically-damped-ish step)
        self.v_cpu = lerp(self.v_cpu, avg / 100.0, 0.25);
        self.v_mem = lerp(self.v_mem, mem, 0.25);

        self.cpu_use = self.v_cpu.clamp(0.0, 1.0);
        self.mem_use = self.v_mem.clamp(0.0, 1.0);
    }
}

#[inline]
fn lerp(a: f32, b: f32, t: f32) -> f32 {
    a + (b - a) * t
}

