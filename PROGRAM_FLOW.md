┌─────────────────────┐
                         │       main.c         │
                         │  parse argv, resolve  │
                         │   config path         │
                         └──────────┬───────────┘
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │     config_load()     │◄── reads notif-archiver.conf
                         │  (config.c)            │    (archive_root, apps list,
                         │  fills config_t cfg    │     screenshot_delay_ms)
                         └──────────┬───────────┘
                                    │
                         apps==0? ──┴── fatal, exit 1
                                    │
                                    ▼
                         ┌─────────────────────┐
                         │  bus_listener_run()   │
                         │  (bus_listener.c)      │
                         └──────────┬───────────┘
                                    │
                        ┌───────────┴────────────┐
                        │   connect_bus()          │
                        │   try eavesdrop match     │
                        │   rule first ──fail──►    │
                        │   fall back to             │
                        │   BecomeMonitor            │
                        └───────────┬────────────┘
                                    │
                                    ▼
                    ┌───────────────────────────────┐
                    │   for(;;) event loop:            │
                    │   sd_bus_process() / sd_bus_wait()│
                    │   (blocks until bus activity)     │
                    └───────────────┬───────────────┘
                                    │
                     WhatsApp/Telegram calls Notify()
                     on the session bus  ───────────►│
                                    │
                                    ▼
                    ┌───────────────────────────────┐
                    │   on_message() callback           │
                    │   (bus_listener.c)                 │
                    │   filter: is this Notify from an   │
                    │   app in our config's [apps] list? │
                    └───────────────┬───────────────┘
                            no ─────┴───── yes
                          (ignore)         │
                                           ▼
              ┌────────────────────────────────────────────┐
              │                                                │
              ▼                                                ▼
  ┌───────────────────────┐                    ┌───────────────────────────┐
  │  storage_build_dir()     │                    │   screenshot_capture()      │
  │  (storage.c)               │                    │   (screenshot.c)             │
  │  archive_root/App/Date/    │                    │   fork() + exec(grim/scrot)  │
  │  mkdir -p equivalent       │                    │   waitpid() for result       │
  └───────────┬───────────┘                    └─────────────┬─────────────┘
              │                                                │
              │                                                │
              ▼                                                │
  ┌───────────────────────┐                                  │
  │   parser_split()          │                                  │
  │   (parser.c)                │                                  │
  │   split "Sender: msg" for   │                                  │
  │   group chats, or treat     │                                  │
  │   summary=sender for 1:1    │                                  │
  └───────────┬───────────┘                                  │
              │                                                │
              └──────────────────┬─────────────────────────────┘
                                  ▼
                    ┌───────────────────────────┐
                    │   storage_write_entry()      │
                    │   (storage.c)                  │
                    │   JSON-escape + append one     │
                    │   line to dir/log.jsonl        │
                    └───────────────────────────┘

  Every function call above that can fail logs through log_msg()
  (log.c) → timestamped, leveled output to stdout/stderr, visible
  in `journalctl --user -u notif-archiver` when run under systemd.
