# ticklab

A from-scratch study of how time-sensitive distributed systems work: several machines running on a shared clock, exchanging signals over an unreliable network, and staying in sync.

The work the system performs is deliberately trivial. What matters is the architecture around it: transport, identity, lifecycle, timing, synchronization, and surviving a bad network. Each one is built and tested on its own before the next one is added.

## The plan

| Stage | Layer                               | Goal                                                                                                |
| :---: | ----------------------------------- | --------------------------------------------------------------------------------------------------- |
| **1** | **UDP transport + message framing** | Send and receive datagrams on every platform, wrapped in a fixed binary header (sequence number, tick, type, payload length). |
| **2** | **Session & identity**              | Know who is connected: handshakes, stable peer IDs, and detecting peers that disconnect.            |
| **3** | **Lifecycle state machine**         | Define explicit states for when a session starts, runs and ends, and which events move it between them. |
| **4** | **Tick clock**                      | A shared, fixed-rate clock that every machine agrees on and that drives all of the work.            |
| **5** | **Authoritative sync + reconciliation** | One source of truth for state; peers that drift from it are corrected back into line.           |
| **6** | **Network simulation**              | Inject packet loss, jitter and reordering on purpose to show that every earlier stage holds up.      |

Each stage builds only on the stages before it.

## Approach

- **C++20, no networking libraries.** Sockets, framing, sequencing and sync are all written by hand, because building them is the point of the project.
- **One module per stage** under `src/`, each exposing a small interface. For example, `transport` hides the POSIX and Winsock socket code behind a single `UdpSocket` class.
- **Tested in isolation** with GoogleTest, fetched through CMake. It's the one deliberate exception to the no-libraries rule, since testing tools aren't part of what's being studied.
- **Real machines, not threads.** It's built to run across a Windows PC, a Linux laptop and a remote VPS, so timing, latency and packet loss come from a real network.
