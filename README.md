# ticklab

A from-scratch study of how time-sensitive distributed systems work: several machines running on a shared clock, exchanging signals over an unreliable network, and staying in sync.

The work the system performs is deliberately trivial. What matters is the architecture around it: transport, identity, reliability, timing, lifecycle, synchronization, and surviving a bad network. Each one is built and tested on its own before the next one is added.

## Vocabulary

Every term has exactly one meaning, and each one belongs to one layer.

| Term | Meaning | Layer |
|---|---|---|
| **frame** | One UDP datagram: header plus payload. | transport |
| **message** | One unit of meaning carried inside a frame (`Hello`, `Heartbeat`, an event). | above transport |
| **peer** | "The machine at the other end", used loosely. Not a code concept. | — |
| **server / client** | Roles. The VPS is the server; the PC and laptop are clients. | — |
| **client ID** | A stable identity. It survives reconnects. | identity |
| **connection** | One live, identified link between a client and the server. It ends on timeout or `Goodbye`. A reconnect creates a new connection bound to the same client ID. | connection |
| **session** | One run of the activity: a set of clients, a start, running, an end. | lifecycle |
| **tick** | One step of shared time. | clock |

## The plan

Seven stages, each one a layer with a single job, built in the order below.

| # | Stage | Goal |
|:---:|---|---|
| **1** | **Transport & framing** | Send and receive datagrams on every platform, wrapped in a fixed binary header (sequence number, tick, type, payload length). Detect gaps, reordering and duplicates. |
| **2** | **Connection & identity** | Client–server handshake, a stable client ID with a reconnect token, and liveness through heartbeats and timeouts. |
| **3** | **Network conditioner** | Inject loss, jitter, reordering and duplication on purpose. A test instrument used by every stage after it. |
| **4** | **Reliability** | Acks, round-trip-time measurement, retransmission of messages, and delivery channels (reliable-ordered, unreliable latest-wins, redundant). |
| **5** | **Tick clock** | A shared, fixed-rate clock that every machine agrees on, built on the round-trip time from stage 4. |
| **6** | **Session lifecycle** | Explicit states for when a session starts, runs and ends, with transitions that happen on agreed ticks. |
| **7** | **Authoritative sync & reconciliation** | One source of truth for state; clients that drift from it are corrected back into line. |

Each stage builds only on the stages before it.

### Why this order

- **Connection comes before reliability.** An ack says "I received *your* frame N". That only means something once you know who "you" is and have per-connection state. The handshake therefore does its own simple resend instead of relying on the reliability layer.
- **The network conditioner comes before reliability.** A real network rarely loses frames, so retransmission can't be tested without injecting loss on purpose. From stage 4 onward, every system test also runs through the conditioner.
- **Reliability comes before the clock.** Acks give round-trip time, and clock synchronization is built on round-trip time.
- **The clock comes before the lifecycle.** "The session starts at tick 1000" is a single, testable moment on every machine. "The session starts when the message arrives" is a different moment on each machine.

## Approach

- **C++20, no networking libraries.** Sockets, framing, sequencing, reliability and sync are all written by hand, because building them is the point of the project.
- **One module per stage** under `src/`, each exposing a small interface. For example, `transport` hides the POSIX and Winsock socket code behind a single `UdpSocket` class.
- **Tested in isolation** with GoogleTest, fetched through CMake. It's the one deliberate exception to the no-libraries rule, since testing tools aren't part of what's being studied.
- **Real machines, not threads.** It's built to run across a Windows PC, a Linux laptop and a remote VPS, so timing, latency and packet loss come from a real network.

## Known limitations

- **Loss at the end of a stream is invisible.** The sequence tracker only notices a gap when a later frame arrives. Stage 2's heartbeats keep frames flowing, which makes trailing loss visible.
- **The VPS leg is untested.** Stage 1 was verified between the PC and the laptop. Stage 2's system test runs the server on the VPS.
