# Remote keyboard/mouse support (remotekbd)

## Synopsis

This is a small utility to capture the mouse and keyboard input from one
machine, and transmit them to another. Basically like a remote desktop,
but without the desktop (screen) bit.

Why? Well, I have a Raspberry Pi fileserver that's just close enough to
the main desktop that it can plug in to the monitor's spare input port so
I can use the display's buttons to quickly switch between them - and I
can see what it's doing that way if it's not running X which I don't in
general want it to. I can even see its console if it loses networking.
Both of those would stop solutions like vnc from working.

On the other hand unless it's got into enough trouble that a directly
attached keyboard is the only way to talk to it, I'd rather avoid the
clutter of having an additional keyboard/mouse permanently attached.


## Building

Requires libssh and its development package.

"make" should produce the two bare binaries kbd-snd and kbd-rcv.

"make rpm" on an RPM based system should produce an RPM containing those
under rpm_base/RPMS/.

"make deb" on a Debian based system should produce a .deb under deb_base/

## Usage (sender)

```
Usage:
  kbd-snd [-l] [-s] [-k kbd] [-m mouse] [target]
    -l --list     : list available keyboard/mouse devices
    -s --ssh      : use SSH to connect to target
    -k --keyboard : select a specific keyboard device
    -m --mouse    : select a specific mouse device
    target        : [user@]host[:port] for SSH
                  : host[:port] for TCP
                  : unspecified for stdout
```

kbd-snd will capture one local keyboard and mouse and transmit the data
elsewhere. Without arguments that means to standard out, and it's your
problem to route that to the other side.

With a target specified it will create a network connection to that
host, raw TCP by default or ssh with "-s", and transmit the captured
input events over that socket.

By default it uses the first keyboard and first mouse it happens to find.
If you have more than one and it picks the wrong ones, you can use "-l",
"-k" and "-m" to push it in the right direction.

Press F8 to tell it to exit.

Because capturing the keyboard/mouse is a privileged operation, it must
be run as root.


## Usage (receiver)

```
Usage:
  kbd-rcv [-l] [-p port]]
    -l --listen   : listen for incoming network connections
    -p --port     : set listen port (default 6890)
```

kbd-rcv will accept captured input data and re-inject it into the machine
it is running on. Without arguments it listens to standard input (which
is used when being run under ssh or similar), with "-l" it is the other
half to kbd-snd's raw TCP mode.

Because injecting input events is also a privileged operation, this must
also be run as root.


## Examples

```
client$ sudo kbd-snd | ssh root@fileserver kbd-rcv
```

Basic operation over a manual ssh channel (though you can replace this
with any transport you like as long as the data gets there).

```
fileserver$ sudo kbd-rcv -l
client$ sudo kbd-snd fileserver
```

Using a raw TCP connection. Obviously you should only use this if you
really trust your local networking environment.

```
client$ sudo kbd-snd -s root@fileserver
```

Connecting over ssh and running the receiver automatically. You should
make sure that the remote server's host key will be automatically
accepted, and that our ssh key will be accepted by the remote host.
Basically if "sudo ssh root@fileserver" gets you a shell without any
questions, the above will likely work.

Note: ssh versions assume that kbd-rcv is on the PATH, which it should
be if you've installed the packaged version.
