"""Network impairment rig for the relay tests.

Two ways to make the ROM-free relay tests run over a bad network:

* In-process (default, deterministic, no privileges): `Impairment` wraps the
  relay's UDP transport and drops, duplicates, delays and reorders snapshot
  datagrams according to a `Profile`; the relay's own fake latency/jitter does
  the same, order preserving, for the TCP ring. The random streams are seeded,
  so a profile produces the same loss pattern on every run.
* Kernel (`--tc`, Linux, needs CAP_NET_ADMIN and the sch_netem module):
  `kernel_netem()` puts a netem qdisc on the loopback device for the duration
  of a scenario so TCP and UDP both cross a real impaired path. When netem is
  unavailable it says so and the caller falls back to the in-process rig.

Loss is a two-state Gilbert-Elliott model: `loss` is the per-datagram loss in
the good state, `burst_enter` the probability per datagram of entering the bad
state, `burst_loss` the loss there and `burst_mean` the mean bad-state length
in datagrams. With `burst_enter=0` it is plain random loss.
"""
import asyncio
import random
import shutil
import subprocess
import time
from dataclasses import dataclass, field


@dataclass(frozen=True)
class Profile:
    name: str
    delay_ms: int = 0         # one-way, applied to UDP snapshots and (via the relay) to TCP
    jitter_ms: int = 0        # uniform +/- jitter; UDP datagrams may overtake each other
    loss: float = 0.0         # random loss in the good state
    burst_enter: float = 0.0  # per-datagram probability of a loss burst starting
    burst_loss: float = 0.0   # loss inside a burst
    burst_mean: float = 1.0   # mean burst length in datagrams
    duplicate: float = 0.0    # probability that a datagram is delivered twice
    reorder: bool = True      # False: jitter never reorders (delivery keeps wire order)
    # Acceptance bounds for the `netem` scenario on localhost at 20 Hz.
    max_stale_ms: int = 300   # worst staleness of a remote car seen by any cabinet
    max_drops: int = 0        # times any cabinet may fall back from UDP-only to TCP

    @property
    def expected_loss(self):
        """Long-run loss fraction of the Gilbert-Elliott model."""
        if not self.burst_enter:
            return self.loss
        leave = 1.0 / max(1.0, self.burst_mean)
        bad = self.burst_enter / (self.burst_enter + leave)
        return (1 - bad) * self.loss + bad * self.burst_loss

    def describe(self):
        parts = [f"{self.delay_ms}+/-{self.jitter_ms} ms"]
        if self.loss:
            parts.append(f"{self.loss * 100:.1f}% loss")
        if self.burst_enter:
            parts.append(f"bursts of ~{self.burst_mean:.0f} at {self.burst_loss * 100:.0f}% (enter {self.burst_enter * 100:.1f}%)")
        if self.duplicate:
            parts.append(f"{self.duplicate * 100:.1f}% duplicates")
        if not self.reorder:
            parts.append("in order")
        return ", ".join(parts) + f" -> ~{self.expected_loss * 100:.1f}% loss"


PROFILES = {
    # Home Wi-Fi: a little random loss and jitter, the occasional duplicate.
    "wifi": Profile("wifi", delay_ms=5, jitter_ms=5, loss=0.02, duplicate=0.005, max_stale_ms=300),
    # Loaded DSL/cable uplink: distance, queueing jitter and bursty loss.
    "congested": Profile("congested", delay_ms=40, jitter_ms=20, loss=0.01, burst_enter=0.02, burst_loss=0.6, burst_mean=3,
                         max_stale_ms=450),
    # Mobile/LTE: long jitter, heavy random loss, frequent reordering.
    "mobile": Profile("mobile", delay_ms=60, jitter_ms=30, loss=0.08, burst_enter=0.01, burst_loss=0.5, burst_mean=4,
                      max_stale_ms=650),
}


@dataclass
class Counters:
    offered: int = 0
    dropped: int = 0
    duplicated: int = 0
    delivered: int = 0
    reordered: int = 0  # delivered before an earlier-offered datagram to the same address

    @property
    def loss(self):
        return self.dropped / self.offered if self.offered else 0.0


class Impairment:
    """Decides the fate of one datagram at a time; delivery goes through `send`."""

    def __init__(self, profile, send, seed=1, loop=None):
        self.profile, self.send, self.loop = profile, send, loop
        self.random = random.Random(seed)
        self.counters = Counters()
        self.bad = False
        self._last_due = {}      # address -> due time of the last datagram offered
        self._next_order = {}    # address -> next order number to deliver (for reorder counting)
        self._seq = {}           # address -> order number of the next datagram offered
        self._pending = {}       # timer id -> handle of a delayed delivery
        self._timer = 0
        self.closed = False

    def close(self):
        """Cancel delayed deliveries (call before closing the transport)."""
        self.closed = True
        for handle in self._pending.values():
            handle.cancel()
        self._pending.clear()

    def _drop(self):
        p = self.profile
        if p.burst_enter:
            if self.bad:
                if self.random.random() < 1.0 / max(1.0, p.burst_mean):
                    self.bad = False
            elif self.random.random() < p.burst_enter:
                self.bad = True
        return self.random.random() < (p.burst_loss if self.bad else p.loss)

    def offer(self, packet, address):
        p = self.profile
        c = self.counters
        c.offered += 1
        order = self._seq.get(address, 0)
        self._seq[address] = order + 1
        if self._drop():
            c.dropped += 1
            self._bump(address, order)
            return
        copies = 2 if p.duplicate and self.random.random() < p.duplicate else 1
        if copies == 2:
            c.duplicated += 1
        for _ in range(copies):
            delay = (p.delay_ms + self.random.uniform(-p.jitter_ms, p.jitter_ms)) / 1000.0 if (p.delay_ms or p.jitter_ms) else 0.0
            if delay <= 0:
                self._deliver(packet, address, order)
                continue
            loop = self.loop or asyncio.get_running_loop()
            due = loop.time() + delay
            if not p.reorder:
                due = max(due, self._last_due.get(address, 0.0))
                self._last_due[address] = due
            self._timer += 1
            self._pending[self._timer] = loop.call_at(due, self._deliver, packet, address, order, self._timer)

    def _bump(self, address, order):
        if self._next_order.get(address, 0) == order:
            self._next_order[address] = order + 1

    def _deliver(self, packet, address, order, timer=None):
        if timer is not None:
            self._pending.pop(timer, None)
        if self.closed:
            return
        c = self.counters
        c.delivered += 1
        expected = self._next_order.get(address, 0)
        if order < expected:
            c.reordered += 1
        else:
            self._next_order[address] = order + 1
        self.send(packet, address)


def netem_args(profile):
    """Arguments for `tc qdisc ... netem` approximating the profile (netem has no Gilbert-Elliott with a separate good-state rate, so the long-run loss is used)."""
    args = []
    if profile.delay_ms or profile.jitter_ms:
        args += ["delay", f"{profile.delay_ms}ms", f"{profile.jitter_ms}ms"]
        if not profile.reorder:
            args += ["rate", "1gbit"]  # keeps packets in order under jitter
    loss = profile.expected_loss
    if loss:
        args += ["loss", f"{loss * 100:.2f}%"]
    if profile.duplicate:
        args += ["duplicate", f"{profile.duplicate * 100:.2f}%"]
    return args


class kernel_netem:
    """Context manager: `with kernel_netem(profile) as active:`; active is False when netem could not be installed."""

    def __init__(self, profile, device="lo", log=print):
        self.profile, self.device, self.log, self.active = profile, device, log, False

    def _tc(self, *args):
        tc = shutil.which("tc") or shutil.which("tc", path="/usr/sbin:/sbin")
        if tc is None:
            return False, "tc not found"
        cmd = [tc] + list(args)
        if shutil.which("sudo") and subprocess.run(["id", "-u"], capture_output=True, text=True).stdout.strip() != "0":
            cmd = ["sudo", "-n"] + cmd
        r = subprocess.run(cmd, capture_output=True, text=True)
        return r.returncode == 0, (r.stderr or r.stdout).strip()

    def __enter__(self):
        args = netem_args(self.profile)
        if not args:
            return False
        ok, msg = self._tc("qdisc", "replace", "dev", self.device, "root", "netem", *args)
        if not ok:
            self.log(f"kernel netem unavailable ({msg}); using the in-process rig")
            return False
        self.active = True
        self.log(f"kernel netem on {self.device}: {' '.join(args)}")
        return True

    def __exit__(self, *exc):
        if self.active:
            self._tc("qdisc", "del", "dev", self.device, "root")
            self.active = False
        return False


if __name__ == "__main__":
    # Quick self-check of the loss model: python3 Tests/netem.py
    for name, p in PROFILES.items():
        imp = Impairment(p, lambda *_: None, loop=None)
        imp.profile = Profile(name, loss=p.loss, burst_enter=p.burst_enter, burst_loss=p.burst_loss, burst_mean=p.burst_mean)
        for _ in range(200000):
            imp.offer(b"", ("x", 0))
        print(f"{name:10s} {p.describe():70s} simulated {imp.counters.loss * 100:.2f}%")
