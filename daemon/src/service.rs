//! Windows Service wrapper for coLinux 2.0 (SCM install/uninstall, event log).
use std::ffi::OsString;
use std::sync::{atomic::{AtomicBool, Ordering}, Arc, OnceLock};
use std::time::Duration;

use anyhow::Result;
use windows_service::{
    define_windows_service, eventlog,
    service::{ServiceControl, ServiceControlAccept, ServiceExitCode, ServiceInfo, ServiceStartType, ServiceState, ServiceType, ServiceErrorControl},
    service_control_handler::{self, ServiceControlHandlerResult},
    service_dispatcher,
    service_manager::{ServiceManager, ServiceManagerAccess},
};

pub const SERVICE_NAME: &str = "coLinux2";
pub const SERVICE_DISPLAY_NAME: &str = "coLinux 2.0 Daemon";
pub const EVENT_SOURCE: &str = "coLinux2";

pub static STOP_FLAG: AtomicBool = AtomicBool::new(false);

// Holds the main loop to run inside the service context
static RUNNER: OnceLock<Arc<dyn Fn() -> Result<()> + Send + Sync + 'static>> = OnceLock::new();

pub fn install_service(bin_path: &str) -> Result<()> {
    let mgr = ServiceManager::local_computer(None::<&str>, ServiceManagerAccess::CREATE_SERVICE)?;
    let service_info = ServiceInfo {
        name: OsString::from(SERVICE_NAME),
        display_name: OsString::from(SERVICE_DISPLAY_NAME),
        service_type: ServiceType::OWN_PROCESS,
        start_type: ServiceStartType::AutomaticDelayedStart,
        error_control: ServiceErrorControl::Normal,
        executable_path: OsString::from(bin_path),
        launch_arguments: vec!["--service-run".into()],
        dependencies: vec![],
        account_name: None, // LocalSystem
        account_password: None,
    };
    let service = mgr.create_service(&service_info, ServiceManagerAccess::empty())?;
    drop(service);
    // event log registration
    let exe = std::env::current_exe()?.to_string_lossy().to_string();
    let _ = eventlog::register(EVENT_SOURCE, &exe);
    Ok(())
}

pub fn uninstall_service() -> Result<()> {
    let mgr = ServiceManager::local_computer(None::<&str>, ServiceManagerAccess::CONNECT)?;
    if let Ok(svc) = mgr.open_service(SERVICE_NAME, windows_service::service::ServiceAccess::all()) {
        let _ = svc.stop();
        svc.delete()?;
    }
    let _ = eventlog::deregister(EVENT_SOURCE);
    Ok(())
}

pub fn maybe_run_as_service<F>(main_loop: F) -> Result<bool>
where
    F: Fn() -> Result<()> + Send + Sync + 'static,
{
    if std::env::args().any(|a| a == "--service-run") {
        let _ = RUNNER.set(Arc::new(main_loop));
        service_dispatcher::start(SERVICE_NAME, ffi_service_main)?;
        return Ok(true);
    }
    Ok(false)
}

define_windows_service!(ffi_service_main, service_main);

fn service_main(_args: Vec<OsString>) {
    let _ = eventlog::register(EVENT_SOURCE, &std::env::current_exe().unwrap().to_string_lossy());

    let status_handle = service_control_handler::register(SERVICE_NAME, move |control| match control {
        ServiceControl::Stop | ServiceControl::Shutdown => {
            STOP_FLAG.store(true, Ordering::SeqCst);
            ServiceControlHandlerResult::NoError
        }
        ServiceControl::Interrogate => ServiceControlHandlerResult::NoError,
        _ => ServiceControlHandlerResult::NotImplemented,
    }).expect("register service ctrl handler");

    let mut status = windows_service::service::ServiceStatus {
        service_type: ServiceType::OWN_PROCESS,
        current_state: ServiceState::StartPending,
        controls_accepted: ServiceControlAccept::STOP | ServiceControlAccept::SHUTDOWN,
        exit_code: ServiceExitCode::Win32(0),
        checkpoint: 0,
        wait_hint: Duration::from_secs(5),
        process_id: None,
    };
    let _ = status_handle.set_service_status(status.clone());

    // run main loop in a thread so we can honor STOP
    let runner = RUNNER
        .get()
        .cloned()
        .expect("service runner not set; maybe_run_as_service not called");
    let handle = std::thread::spawn(move || {
        if let Err(e) = (runner)() {
            let _ = eventlog::write_event(EVENT_SOURCE, eventlog::EventType::Error, &format!("Fatal: {e:?}"));
        }
    });

    status.current_state = ServiceState::Running;
    status.controls_accepted = ServiceControlAccept::STOP | ServiceControlAccept::SHUTDOWN;
    status.wait_hint = Duration::from_secs(0);
    let _ = status_handle.set_service_status(status.clone());

    // wait for stop
    while !STOP_FLAG.load(Ordering::SeqCst) {
        std::thread::sleep(Duration::from_millis(250));
    }

    // graceful shutdown
    status.current_state = ServiceState::StopPending;
    status.wait_hint = Duration::from_secs(5);
    let _ = status_handle.set_service_status(status.clone());

    let _ = handle.join();

    status.current_state = ServiceState::Stopped;
    status.wait_hint = Duration::from_secs(0);
    let _ = status_handle.set_service_status(status);
}

