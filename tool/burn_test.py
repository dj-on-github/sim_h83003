"""End-to-end test of artista180_burn_application against the simulator.

There is no socat here and no libserialport, so the bridge is built rather
than borrowed: this makes a pty pair, points the burner at the slave -- which
is a serial port as far as open() and termios are concerned, so the real
device path is exercised rather than stubbed -- and relays the master to
tool/serial_stdio.dart, which runs the machine with one SCI on its pipes.

    python3 tool/burn_test.py <image.bin> <app.bin> [--channel N] [--out FILE]

The machine is reset by starting it: the burner waits for the boot ROM's
announcement, and the simulator sends it a moment after it starts, so the two
meet without anything having to power-cycle anything.
"""
import os, pty, select, subprocess, sys, time

def main(argv):
    def opt(name, default=None):
        return argv[argv.index(name) + 1] if name in argv else default

    args = [a for a in argv[1:] if not a.startswith('--')]
    for n in ('--channel', '--out', '--baud'):
        v = opt(n)
        if v in args:
            args.remove(v)
    if len(args) < 2:
        print(__doc__)
        return 2

    image, appbin = args[0], args[1]
    channel = opt('--channel', '1')
    out = opt('--out')
    baud = opt('--baud', '115200')
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    master, slave = pty.openpty()
    port = os.ttyname(slave)
    print('pty: %s' % port, flush=True)

    sim = subprocess.Popen(
        ['dart', 'run', 'tool/serial_stdio.dart', image,
         '--channel', channel, '--seconds', '1800']
        + (['--dump', out] if out else []),
        cwd=root, stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    burner = subprocess.Popen(
        [os.path.join(root, 'host', 'artista180_burn_application'),
         '-b', baud, '-w', '90', port, appbin],
        cwd=root, stdin=slave, stdout=slave, stderr=None)
    os.close(slave)

    # Relay until the burner finishes and the wire goes quiet.
    to_sim, to_pty = 0, 0
    deadline = time.time() + 900
    while time.time() < deadline:
        if burner.poll() is not None and to_pty == to_pty:
            # let anything still in flight land
            pass
        r, _, _ = select.select([master, sim.stdout], [], [], 0.05)
        if master in r:
            try:
                data = os.read(master, 4096)
            except OSError:
                data = b''
            if data:
                sim.stdin.write(data)
                sim.stdin.flush()
                to_sim += len(data)
        if sim.stdout in r:
            data = os.read(sim.stdout.fileno(), 4096)
            if data:
                try:
                    os.write(master, data)
                    to_pty += len(data)
                except OSError:
                    # the burner has closed the slave and gone
                    break
        if burner.poll() is not None and not r:
            break

    rc = burner.wait(timeout=30)
    print('burner exit %d; %d bytes to the machine, %d back'
          % (rc, to_sim, to_pty), flush=True)

    # Give the machine a moment to reset and start what was just burned, then
    # take the memory it ends up with if a dump was asked for.
    if out and rc == 0:
        time.sleep(3)
    try:
        sim.stdin.close()
    except Exception:
        pass
    try:
        sim.wait(timeout=20)
    except subprocess.TimeoutExpired:
        sim.terminate()
    os.close(master)
    return rc

if __name__ == '__main__':
    sys.exit(main(sys.argv))
