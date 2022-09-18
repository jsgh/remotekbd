#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/uinput.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <getopt.h>

#include "uinames.h"
#include "poller.h"

#include <vector>
#include <unordered_set>

#define LPORT 6890

int fds[2] = { -1, -1 };

int create_dev(int fd, const char *name)
{
  fprintf(stderr, "Creating dev %s...\n", name);
  struct uinput_setup d;
  memset(&d, 0, sizeof(d));
  snprintf(d.name, UINPUT_MAX_NAME_SIZE, name);
  d.id.bustype = BUS_USB;
  d.id.vendor = 0x0001;
  d.id.product = 0x0001;
  d.id.version = 1;
  ssize_t rv = ioctl(fd, UI_DEV_SETUP, &d);
  if (rv==-1 && errno==ENOSYS) {
      struct uinput_user_dev ud;
      memset(&ud, 0, sizeof(ud));
      strcpy(ud.name, d.name);
      ud.id.bustype = d.id.bustype;
      ud.id.vendor = d.id.vendor;
      ud.id.product = d.id.product;
      ud.id.version = d.id.version;
      rv = write(fd, &ud, sizeof(ud));
  }
  if (rv==-1) {
    fprintf(stderr, "Receiver dev setup error %d %s\n", errno, strerror(errno));
    return 1;
  }
  if (ioctl(fd, UI_DEV_CREATE)==-1) {
    fprintf(stderr, "Receiver dev create error %d %s\n", errno, strerror(errno));
    return 1;
  }

  char buf[PATH_MAX+1];
  rv = ioctl(fd, UI_GET_SYSNAME(sizeof(buf)-1), buf);
  if (rv==-1) {
    fprintf(stderr, "Can't get created device name %d %s\n", errno, strerror(errno));
  } else {
    buf[rv] = '\0';
    fprintf(stderr, "Created device %s as '%s'\n", name, buf);
  }

  return 0;
}

int create_kb()
{
  int fd = open("/dev/uinput", O_WRONLY|O_NONBLOCK);
  if (fd==-1) {
    fprintf(stderr, "Can't open '/dev/uinput' %d %s\n", errno, strerror(errno));
    return 1;
  }

  int version;
  if (ioctl(fd, UI_GET_VERSION, &version)==-1) {
    fprintf(stderr, "uinput version not available %d %s\n", errno, strerror(errno));
  } else {
    fprintf(stderr, "uinput version %d\n", version);
  }

  fprintf(stderr, "Setting bits...\n");
  if (ioctl(fd, UI_SET_EVBIT, EV_SYN)==-1) return 1;
  if (ioctl(fd, UI_SET_EVBIT, EV_KEY)==-1) return 1;
  if (ioctl(fd, UI_SET_EVBIT, EV_MSC)==-1) return 1;
  if (ioctl(fd, UI_SET_EVBIT, EV_REP)==-1) return 1;
  for ( int i=0 ; i<KEY_MAX ; i++ ) {
    if (is_code_key(i)) {
      if (ioctl(fd, UI_SET_KEYBIT, i)==-1) return 1;
    }
  }

  int rv = create_dev(fd, "remotekbd-rcv-kbd");
  if (rv) return rv;

  fds[0] = fd;
  return 0;
}

int create_mouse()
{
  int fd = open("/dev/uinput", O_WRONLY|O_NONBLOCK);
  if (fd==-1) {
    fprintf(stderr, "Can't open '/dev/uinput' %d %s\n", errno, strerror(errno));
    return 1;
  }

  int version;
  if (ioctl(fd, UI_GET_VERSION, &version)==-1) {
    fprintf(stderr, "uinput version not available %d %s\n", errno, strerror(errno));
  } else {
    fprintf(stderr, "uinput version %d\n", version);
  }

  fprintf(stderr, "Setting bits...\n");
  if (ioctl(fd, UI_SET_EVBIT, EV_SYN)==-1) return 1;
  if (ioctl(fd, UI_SET_EVBIT, EV_KEY)==-1) return 1;
  if (ioctl(fd, UI_SET_EVBIT, EV_REL)==-1) return 1;
  if (ioctl(fd, UI_SET_EVBIT, EV_MSC)==-1) return 1;
  for ( int i=0 ; i<KEY_MAX ; i++ ) {
    if (is_code_mouse(i)) {
      if (ioctl(fd, UI_SET_KEYBIT, i)==-1) return 1;
    }
  }
  if (ioctl(fd, UI_SET_RELBIT, REL_X)==-1) return 1;
  if (ioctl(fd, UI_SET_RELBIT, REL_Y)==-1) return 1;
  if (ioctl(fd, UI_SET_RELBIT, REL_WHEEL)==-1) return 1;
  if (ioctl(fd, UI_SET_RELBIT, REL_HWHEEL)==-1) return 1;

  int rv = create_dev(fd, "remotekbd-rcv-mouse");
  if (rv) return rv;

  fds[1] = fd;
  return 0;
}

Poller poller;

class Conn : public PollItem
{
public:
  Conn(Poller *poller_, int fd_, bool noclose_=false, bool quitonclose_=false)
  {
    noclose = noclose_;
    quitonclose = quitonclose_;
    fd = fd_;
    if (poller_->AddPollItem(this, EPOLLIN)) {
      fprintf(stderr, "Receiver can't register Conn %d %s\n", errno, strerror(errno));
      close(fd);
      fd = -1;
    }
  }

  ~Conn()
  {
    if (poller) poller->RemovePollItem(this);
    if (fd!=-1 && !noclose) {
      close(fd);
      fd = -1;
    }
  }

  void on_events(uint32_t events)
  {
    bool closed = false;
    if (events & EPOLLIN) {
      if (DoRead()) {
        closed = true;
      }
    }
    if (!closed && (events & EPOLLHUP)) {
      closed = true;
    }
    if (closed) {
      clear_all_keys(0);
      clear_all_keys(1);
      if (quitonclose) {
        poller->Quit();
      }
      poller->RemovePollItem(this);
      delete this;
    }
  }

  std::string ln;
  struct iev {
    uint16_t t;
    uint16_t c;
    int32_t v;
  };
  std::vector<iev> ievs[2];

  std::unordered_set<int> downkeys[2];

  void clear_all_keys(int w)
  {
    if (downkeys[w].empty()) return;

    fprintf(stderr, "Forcing up %zu down %s\n", downkeys[w].size(), w ? "mouse" : "kbd");
    struct input_event e;
    e.time.tv_sec = 0;
    e.time.tv_usec = 0;
    e.type = EV_SYN;
    e.code = SYN_REPORT;
    e.value = 0;
    ssize_t rv = write(fds[w], &e, sizeof(e));
    for ( const auto &k: downkeys[w] ) {
      e.time.tv_sec = 0;
      e.time.tv_usec = 0;
      e.type = EV_KEY;
      e.code = k;
      e.value = 0;
      rv = write(fds[w], &e, sizeof(e));
    }
    e.time.tv_sec = 0;
    e.time.tv_usec = 0;
    e.type = EV_SYN;
    e.code = SYN_REPORT;
    e.value = 0;
    rv = write(fds[w], &e, sizeof(e));
  }

  int parse_line()
  {
    int w, t, c, v;
    if (sscanf(ln.c_str(), "%02x%04x%04x%08x", &w, &t, &c, &v)==4 && (w==0 || w==1)) {
      ievs[w].push_back({(uint16_t)t, (uint16_t)c, (int32_t)v});
      if (t==EV_SYN && c==SYN_REPORT) {
        for ( size_t i=0 ; i<ievs[w].size() ; i++ ) {
          struct input_event e;
          e.time.tv_sec = 0;
          e.time.tv_usec = 0;
          e.type = ievs[w][i].t;
          e.code = ievs[w][i].c;
          e.value = ievs[w][i].v;
          ssize_t rv = write(fds[w], &e, sizeof(e));
          if (rv==-1) {
            fprintf(stderr, "Receiver write error %d %s\n", errno, strerror(errno));
            return 3;
          }
        }
        ievs[w].clear();
      }
    } else {
      fprintf(stderr, "Bad line '%s'\n", ln.c_str());
    }
    ln.clear();
    return 0;
  }

  int DoRead()
  {
    char buf[4096];
    ssize_t rv = read(fd, buf, sizeof(buf));
    if (rv==-1) {
      if (errno==EAGAIN) return 0;
      fprintf(stderr, "Receiver read error %d %s\n", errno, strerror(errno));
      return 2;
    }
    if (rv==0) return 3;
    ssize_t s = 0;
    while (s<rv) {
      char *p = (char*)memchr(buf+s, '\n', rv);
      if (p) {
        size_t n = p - (buf+s);
        if (n>0) ln.append(buf+s, n);
        int pr = parse_line();
        if (pr) {
          return pr;
        }
        s += n+1;
      } else {
        ln.append(buf+s, rv-s);
        break;
      }
    }
    return 0;
  }

  bool quitonclose;
  bool noclose;
};

class Listener : public PollItem
{
public:
  Listener(Poller *poller_, int lport)
  {
    fd = socket(AF_INET, SOCK_STREAM, 0);
    sock_t addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin.sin_family = AF_INET;
    addr.sin.sin_port = htons(lport);
    socklen_t addrlen = sizeof(addr);
    bind(fd, &addr.sa, addrlen);
    listen(fd, 1000);
    poller_->AddPollItem(this, EPOLLIN);
  }

  ~Listener()
  {
    if (poller) poller->RemovePollItem(this);
    if (fd!=-1) {
      close(fd);
      fd = -1;
    }
  }

  void on_events(uint32_t events)
  {
    if (events & EPOLLIN) {
      sock_t addr;
      socklen_t addrlen = sizeof(addr);
      int cfd = accept(fd, &addr.sa, &addrlen);
      if (cfd!=-1) {
        fprintf(stderr, "Got new connection...\n");
        int fl = fcntl(cfd, F_GETFL);
        fcntl(cfd, F_SETFL, fl|O_NONBLOCK);
        Conn *c = new Conn(poller, cfd);
        if (c->fd==-1) delete c;
      }
    }
  }
};

void usage()
{
  fprintf(stderr,
          "Usage:\n"
          "  kbd-rcv [-l] [-p port]]\n"
          "    -l --listen   : listen for incoming network connections\n"
          "    -p --port     : set listen port (default %d)\n",
          LPORT
    );
}

int main(int argc, char *argv[])
{
  bool netlisten = false;
  bool haveport = false;
  bool argerr = false;
  int lport = LPORT;

  while (1) {
    int i = 0;
    static const struct option lopts[] = {
      { "listen", no_argument, 0, 'l' },
      { "port", required_argument, 0, 'p' },
    };
    int c = getopt_long(argc, argv, "lp:", lopts, &i);
    if (c==-1) break;
    switch (c) {
    case 'l': netlisten = true; break;
    case 'p': lport = atoi(optarg); haveport = true; break;
    case '?': argerr = true; break;
    default: break;
    }
  }

  if (optind<argc) {
    fprintf(stderr, "Unexpected argument\n");
    argerr = true;
  }

  if (argerr || (haveport && !netlisten) || lport<1 || lport>65535) {
    usage();
    return 1;
  }

  if (create_kb()) return 1;
  if (create_mouse()) return 1;

  fprintf(stderr, "Waiting for input...\n");

  poller.Create();

  if (!netlisten) {
    fprintf(stderr, "Reading from stdin...\n");
    Conn *c = new Conn(&poller, 0, true, true);
    if (c->fd==-1) {
      delete c;
      poller.Quit();
    }
  } else {
    fprintf(stderr, "Listening on port %d\n", lport);
    new Listener(&poller, lport);
  }
  poller.Run();

  return 0;
}
