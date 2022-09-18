#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <linux/uinput.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <libssh/libssh.h>
#include <pwd.h>
#include <getopt.h>

#include <string>
#include <vector>
#include <unordered_map>

#include "uinames.h"
#include "poller.h"

std::string dev_dir("/dev/input/");
std::string dev_byid("/dev/input/by-id/");
std::string dev_bypath("/dev/input/by-path/");

#define LPORT 6890
#define SSHPORT 22

std::unordered_map<std::string, std::string> devnames;
struct devinfo {
  std::string path;
  std::string name;
};
std::vector<devinfo> kdevs;
std::vector<devinfo> mdevs;
int kidx = -1;
int midx = -1;

void have_dev(std::vector<devinfo> &l, const char *n)
{
  char buf[PATH_MAX+1];
  std::string lpath = dev_bypath + n;
  ssize_t rv = readlink(lpath.c_str(), buf, sizeof(buf)-1);
  if (rv!=-1) {
    buf[rv] = '\0';
    std::string name;
    auto it = devnames.find(buf);
    if (it!=devnames.end()) name = it->second;
    if (buf[0]=='/') {
      l.push_back({buf , name});
    } else {
      std::string dpath = dev_bypath + buf;
      if (realpath(dpath.c_str(), buf)) {
        l.push_back({buf, name});
      }
    }
  }
}

int select_dev(std::vector<devinfo> &l, const std::string &s)
{
  for ( int i=0 ; i<l.size() ; i++ ) {
    if (s.length()==0) return i;
    if (strcasestr(l[i].path.c_str(), s.c_str())) return i;
    if (strcasestr(l[i].name.c_str(), s.c_str())) return i;
  }
  return -1;
}

void strip_prefix(std::string &s, const char *pfx)
{
  size_t n = strlen(pfx);
  if (s.length()>=n && strncmp(s.c_str(), pfx, n)==0) {
    s = s.substr(n);
  }
}

void strip_suffix(std::string &s, const char *sfx)
{
  size_t n = strlen(sfx);
  if (s.length()>=n && strncmp(s.c_str()+(s.length()-n), sfx, n)==0) {
    s.resize(s.length()-n);
  }
}

void tr(std::string &s, char cfrom, char cto)
{
  for ( size_t i=0 ; i<s.length() ; i++ ) {
    if (s[i]==cfrom) s[i] = cto;
  }
}

void enum_devs()
{
  DIR *d = opendir(dev_byid.c_str());
  if (d) {
    while (true) {
      struct dirent *e = readdir(d);
      if (!e) break;

      if (strstr(e->d_name, "-event-kbd") ||
          strstr(e->d_name, "-event-mouse")) {
        char buf[PATH_MAX+1];
        std::string lpath = dev_byid + e->d_name;
        ssize_t rv = readlink(lpath.c_str(), buf, sizeof(buf)-1);
        if (rv!=-1) {
          buf[rv] = '\0';
          std::string name = e->d_name;
          strip_prefix(name, "usb-");
          strip_suffix(name, "-event-kbd");
          strip_suffix(name, "-event-mouse");
          tr(name, '_', ' ');
          devnames[buf] = name;
        }
      }
    }
    closedir(d);
  }

  d = opendir(dev_bypath.c_str());
  if (d) {
    while (true) {
      struct dirent *e = readdir(d);
      if (!e) break;

      if (strstr(e->d_name, "-event-kbd")) {
        have_dev(kdevs, e->d_name);
      } else if (strstr(e->d_name, "-event-mouse")) {
        have_dev(mdevs, e->d_name);
      }
    }

    closedir(d);
  }
}

class Out : public PollItem
{
public:
  virtual int Add(Poller *poller_) = 0;
  virtual int DoWrite(const char *data, size_t len) = 0;

  virtual int connect_socket(const sock_t &addr, const std::string &host)
  {
    fd = socket(addr.sa.sa_family, SOCK_STREAM, 0);
    if (fd==-1) {
      fprintf(stderr, "Can't create output socket %d %s\n", errno, strerror(errno));
      return 1;
    }
    socklen_t addrlen = sizeof(addr);
    int rv = connect(fd, &addr.sa, addrlen);
    if (rv==-1) {
      fprintf(stderr, "Can't connect to %s: %d %s\n", host.c_str(), errno, strerror(errno));
      close(fd);
      fd = -1;
      return 1;
    }
    int optval = 1;
    socklen_t optlen = sizeof(optval);
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &optval, optlen);
    return 0;
  }
};

class StdOut : public Out
{
public:
  StdOut()
  {
    fd = 1;
  }

  int Add(Poller *poller_)
  {
    return 0;
  }

  void on_events(uint32_t events)
  {
  }

  int DoWrite(const char *data, size_t len)
  {
    return write(1, data, len);
  }
};

class RawOut : public Out
{
public:
  RawOut(const sock_t &addr_, const std::string &host_) : addr(addr_), host(host_)
  {
    if (!addr.sin.sin_port) addr.sin.sin_port = htons(LPORT);
    connect_socket(addr, host);
  }

  int Add(Poller *poller_)
  {
    int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl|O_NONBLOCK);
    return poller_->AddPollItem(this, EPOLLIN);
  }

  void on_events(uint32_t events)
  {
    char buf[4096];
    size_t nr = read(fd, buf, sizeof(buf));
    if (nr>0) {
      ssize_t nw = write(2, buf, nr);
      (void)nw;
    }
    if ((nr==-1 && errno!=EAGAIN) || nr==0) {
      poller->Quit();
    }
  }

  int DoWrite(const char *data, size_t len)
  {
    return write(fd, data, len);
  }

  sock_t addr;
  std::string host;
};

class SshOut : public Out
{
public:
  SshOut(const sock_t &addr_, const std::string &host_, const std::string &user_)
    : addr(addr_), host(host_), user(user_)
  {
    if (!addr.sin.sin_port) addr.sin.sin_port = htons(SSHPORT);
    if (connect_socket(addr, host)) return;
    sess = ssh_new();
    ssh_options_set(sess, SSH_OPTIONS_HOST, host.c_str());
    ssh_options_set(sess, SSH_OPTIONS_USER, user.c_str());
    int opt = 1;
    ssh_options_set(sess, SSH_OPTIONS_PUBKEY_AUTH, &opt);
    socket_t sshfd = fd;
    ssh_options_set(sess, SSH_OPTIONS_FD, &sshfd);
    int rv = ssh_connect(sess);
    if (rv!=SSH_OK) {
      fprintf(stderr, "Can't connect to %s: %s\n", host.c_str(), ssh_get_error(sess));
      close(fd);
      fd = -1;
      return;
    }
    fprintf(stderr, "SSH connected\n");
    rv = ssh_session_is_known_server(sess);
    if (rv!=SSH_KNOWN_HOSTS_OK) {
      fprintf(stderr, "Server is not in known_hosts %d\n", rv);
      close(fd);
      fd = -1;
      return;
    }
    rv = ssh_userauth_autopubkey(sess, nullptr);
    fprintf(stderr, "SSH auth = %d\n", rv);

    ch = ssh_channel_new(sess);
    rv = ssh_channel_open_session(ch);
    rv = ssh_channel_request_exec(ch, "kbd-rcv");
  }

  int Add(Poller *poller_)
  {
    int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl|O_NONBLOCK);
    return poller_->AddPollItem(this, EPOLLIN);
  }

  void on_events(uint32_t events)
  {
    bool closed = false;
    char rbuf[4096];
    if (events & EPOLLIN) {
      int nr = ssh_channel_read_nonblocking(ch, rbuf, sizeof(rbuf), 1);
      if (nr>0) {
        ssize_t nw = write(2, rbuf, nr);
        (void)nw;
      }
      if (nr==SSH_ERROR || ssh_channel_is_eof(ch)) {
        if (nr==SSH_ERROR) {
          fprintf(stderr, "SSH stderr read error %s\n", ssh_get_error(sess));
          errno = ECONNRESET;
        }
        closed = true;
      }
    }
    if (!closed && (events & EPOLLIN)) {
      int nr = ssh_channel_read_nonblocking(ch, rbuf, sizeof(rbuf), 0);
      if (nr>0) {
        ssize_t nw = write(2, rbuf, nr);
        (void)nw;
      }
      if (nr==SSH_ERROR || ssh_channel_is_eof(ch)) {
        if (nr==SSH_ERROR) {
          fprintf(stderr, "SSH stdout read error %s\n", ssh_get_error(sess));
          errno = ECONNRESET;
        }
        closed = true;
      }
    }
    if (closed) {
      poller->Quit();
    }
  }

  int DoWrite(const char *data, size_t len)
  {
    int nw = ssh_channel_write(ch, data, len);
    if (nw==SSH_ERROR) {
      fprintf(stderr, "SSH stdin write error %s\n", ssh_get_error(sess));
      errno = ECONNRESET;
      return -1;
    }
    return nw;
  }

  sock_t addr;
  std::string host;
  std::string user;
  ssh_session sess;
  ssh_channel ch;
};

Poller poller;
Out *out = nullptr;

class Conn : public PollItem
{
public:
  Conn(int w_, const char *path_)
    : w(w_), path(path_), evpktc(0),
      relx(0), rely(0), nrels(0), srelx(0), srely(0), snrels(0)
  {
    fd = open(path.c_str(), O_RDONLY);
    if (fd==-1) {
      fprintf(stderr, "Can't open %s '%s'\n", w ? "mouse" : "kbd", path.c_str());
      return;
    }
  }

  bool all_up()
  {
    char buf[(KEY_MAX+7)/8];
    bool down = false;
    if (ioctl(fd, EVIOCGKEY(sizeof(buf)), buf)==-1) return 1;
    for ( int z=0 ; z<(KEY_MAX+7)/8 ; z++ ) {
      if (buf[z]) down = true;
    }
    return !down;
  }

  int grab()
  {
    if (ioctl(fd, EVIOCGRAB, 1)==-1) return 1;
    return 0;
  }

  void Add(Poller *poller_)
  {
    int fl = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fl|O_NONBLOCK);
    poller_->AddPollItem(this, EPOLLIN);
  }

  void on_events(uint32_t events)
  {
    bool closed = false;
    if (events & EPOLLIN) {
      if (DoRead()) {
        closed = true;
      }
    }
    if (towrite.length()>0) {
      if (DoWrite()) {
        closed = true;
      }
    }
    if (closed) {
      poller->Quit();
    }
  }

  int DoRead()
  {
    char cbuf[32];

    while (true) {
      struct input_event e;
      ssize_t rv = read(fd, &e, sizeof(e));
      if (rv==0) return 1;
      if (rv==-1) {
        if (errno==EAGAIN) break;
        fprintf(stderr, "read error %d %s\n", errno, strerror(errno));
        return 1;
      }

      if (e.type==EV_KEY && e.code==KEY_F8 && e.value) {
        fprintf(stderr, "Got escape key, exiting...\n");
        return 3;
      }

      if (e.type==EV_REL && (e.code==REL_X || e.code==REL_Y)) {
        if (e.code==REL_X) srelx += e.value;
        else if (e.code==REL_Y) srely += e.value;
        snrels++;
        continue;
      }

      if (e.type==EV_SYN && e.code==SYN_REPORT) {
        relx += srelx;
        srelx = 0;
        rely += srely;
        srely = 0;
        nrels += snrels;
        snrels = 0;
        if ((relx!=0 || rely!=0) && evpktc>0) {
          add_rels();
        }
      }

      sprintf(cbuf, "%02x%04x%04x%08x\n", w, e.type, e.code, e.value);
      evpkts += cbuf;
      evpktc++;

      if (e.type==EV_SYN && e.code==SYN_REPORT) {
        if (evpktc>1) {
          towrite += evpkts;
        }
        evpkts.clear();
        evpktc = 0;
      }
    }

    if (nrels>0) {
      if (relx!=0 || rely!=0) {
        if (relx!=0) {
          sprintf(cbuf, "%02x%04x%04x%08x\n", w, EV_REL, REL_X, relx);
          towrite += cbuf;
          relx = 0;
        }
        if (rely!=0) {
          sprintf(cbuf, "%02x%04x%04x%08x\n", w, EV_REL, REL_Y, rely);
          towrite += cbuf;
          rely = 0;
        }
        sprintf(cbuf, "%02x%04x%04x%08x\n", w, EV_SYN, SYN_REPORT, 0);
        towrite += cbuf;
        nrels = 0;
      }
    }

    return 0;
  }

  void add_rels()
  {
    char cbuf[32];

    if (relx!=0) {
      sprintf(cbuf, "%02x%04x%04x%08x\n", w, EV_REL, REL_X, relx);
      evpkts += cbuf;
      evpktc++;
      relx = 0;
    }
    if (rely!=0) {
      sprintf(cbuf, "%02x%04x%04x%08x\n", w, EV_REL, REL_Y, rely);
      evpkts += cbuf;
      evpktc++;
      rely = 0;
    }
    nrels = 0;
  }

  int DoWrite()
  {
    if (towrite.length()>0) {
      ssize_t nw = out->DoWrite(towrite.c_str(), towrite.length());
      if (nw==-1 && errno!=EAGAIN) {
        fprintf(stderr, "Sender write error %d %s\n", errno, strerror(errno));
        return 1;
      }
      if (nw>0) {
        if (nw==towrite.length()) {
          towrite.clear();
        } else {
          towrite = towrite.substr(nw);
        }
      }
      if (towrite.length()>0) poller->Mod(this, EPOLLIN|EPOLLOUT);
    } else {
      poller->Mod(this, EPOLLIN);
    }
    return 0;
  }

  int w;
  std::string path;

  std::string evpkts;
  int evpktc;
  std::string towrite;
  int relx;
  int rely;
  int nrels;
  int srelx;
  int srely;
  int snrels;
};

int grab_devs()
{
  Conn *kconn = new Conn(0, kdevs[kidx].path.c_str());
  if (kconn->fd==-1) {
    return 1;
  }

  Conn *mconn = new Conn(1, mdevs[midx].path.c_str());
  if (mconn->fd==-1) {
    return 1;
  }

  fprintf(stderr, "Waiting for all keys up...\n");

  while (!kconn->all_up() || !mconn->all_up()) {
    usleep(5000);
  }

  if (kconn->grab() || mconn->grab()) return 1;
  fprintf(stderr, "All keys up, starting...\n");

  kconn->Add(&poller);
  mconn->Add(&poller);

  return 0;
}

int resolve(const char *n, sock_t &addr)
{
  if (inet_pton(AF_INET, n, &addr.sin.sin_addr)==1) {
    addr.sin.sin_family = AF_INET;
    return 0;
  } else if (inet_pton(AF_INET6, n, &addr.sin6.sin6_addr)==1) {
    addr.sin6.sin6_family = AF_INET6;
    return 0;
  } else {
    struct addrinfo *ai = nullptr;
    int rv = getaddrinfo(n, nullptr, nullptr, &ai);
    if (rv==0) {
      bool found = false;
      for ( struct addrinfo *p=ai ; p ; p=p->ai_next ) {
        if (!found && p->ai_socktype==SOCK_STREAM && p->ai_addrlen>0 && p->ai_addrlen<=sizeof(addr.ss)) {
          memcpy(&addr.ss, p->ai_addr, p->ai_addrlen);
          found = true;
          break;
        }
      }
      if (!found) {
        fprintf(stderr, "No addresses found for  '%s': %d %s\n", n);
      }
      freeaddrinfo(ai);
      return 0;
    } else {
      fprintf(stderr, "Can't resolve '%s': %d %s\n", n, rv, gai_strerror(rv));
    }
  }
  return 1;
}

std::string addr2str(const sock_t &addr)
{
  char buf[128];
  if (addr.sa.sa_family==AF_INET) {
    inet_ntop(AF_INET, &addr.sin.sin_addr, buf, sizeof(buf));
    if (addr.sin.sin_port) {
      sprintf(buf+strlen(buf), ":%d", ntohs(addr.sin.sin_port));
    }
    return buf;
  } else if (addr.sa.sa_family==AF_INET6) {
    inet_ntop(AF_INET6, &addr.sin6.sin6_addr, buf, sizeof(buf));
    if (addr.sin6.sin6_port) {
      sprintf(buf+strlen(buf), ":%d", ntohs(addr.sin6.sin6_port));
    }
    return buf;
  }
  return "?";
}

void usage()
{
  fprintf(stderr,
          "Usage:\n"
          "  kbd-snd [-l] [-s] [-k kbd] [-m mouse] [target]\n"
          "    -l --list     : list available keyboard/mouse devices\n"
          "    -s --ssh      : use SSH to connect to target\n"
          "    -k --keyboard : select a specific keyboard device\n"
          "    -m --mouse    : select a specific mouse device\n"
          "    target        : [user@]host[:port] for SSH\n"
          "                  : host[:port] for TCP\n"
          "                  : unspecified for stdout\n"
    );
}

int main(int argc, char *argv[])
{
  bool listdevs = false;
  bool ssh = false;
  bool argerr = false;
  int lport = LPORT;
  std::string selkbd;
  std::string selmouse;
  std::string host;
  std::string user;

  while (1) {
    int i = 0;
    static const struct option lopts[] = {
      { "list", no_argument, 0, 'l' },
      { "ssh", no_argument, 0, 's' },
      { "keyboard", required_argument, 0, 'k' },
      { "mouse", required_argument, 0, 'm' },
    };
    int c = getopt_long(argc, argv, "lsk:m:", lopts, &i);
    if (c==-1) break;
    switch (c) {
    case 'l': listdevs = true; break;
    case 's': ssh = true; lport = SSHPORT; break;
    case 'k': selkbd = optarg; break;
    case 'm': selmouse = optarg; break;
    case '?': argerr = true; break;
    default: break;
    }
  }

  if (optind+1<argc) {
    fprintf(stderr, "Unexpected argument\n");
    argerr = true;
  } else if (optind<argc) {
    host = argv[optind];
    if (ssh) {
      size_t p = host.find('@');
      if (p!=std::string::npos) {
        user = host.substr(0, p);
        host = host.substr(p+1);
      } else {
        user = getenv("USER");
        if (user.length()==0) {
          uid_t uid = getuid();
          struct passwd *pw = getpwuid(uid);
          if (pw) user = pw->pw_name;
        }
        if (user.length()==0) {
          fprintf(stderr, "Can't determine user for ssh connection\n");
          argerr = true;
        }
      }
    }
    if (host[0]=='[') {
      size_t he = host.find(']');
      if (he==std::string::npos || (host[he+1]!='\0' && host[he+1]!=':')) {
        fprintf(stderr, "Invalid target\n");
        argerr = true;
      } else {
        if (host[he+1]==':') {
          lport = atoi(host.substr(he+2).c_str());
        }
        host = host.substr(1, he-1);
      }
    } else {
      size_t ci = host.find(':');
      if (ci!=std::string::npos) {
        lport = atoi(host.substr(ci+1).c_str());
        host = host.substr(0, ci);
      }
    }
  }

  if (argerr || (ssh && listdevs) || (ssh && host.length()==0) || lport<1 || lport>65535) {
    usage();
    return 1;
  }

  enum_devs();

  if (listdevs) {
    printf("%zu keyboards:\n", kdevs.size());
    for ( const auto &p: kdevs ) {
      printf("  %s (%s)\n", p.path.c_str(), p.name.c_str());
    }
    printf("%zu mice:\n", mdevs.size());
    for ( const auto &p: mdevs ) {
      printf("  %s (%s)\n", p.path.c_str(), p.name.c_str());
    }
    return 0;
  }

  kidx = select_dev(kdevs, selkbd);
  midx = select_dev(mdevs, selmouse);

  if (kidx==-1 || midx==-1) {
    if (kidx==-1) {
      fprintf(stderr, "Can't find keyboard\n");
    }
    if (midx==-1) {
      fprintf(stderr, "Can't find mouse\n");
    }
    usage();
    return 1;
  }

  if (host.length()>0) {
    sock_t addr;
    memset(&addr, 0, sizeof(addr));
    if (resolve(host.c_str(), addr)) {
      fprintf(stderr, "Can't parse destination address '%s'\n", host.c_str());
      return 1;
    }
    addr.sin.sin_port = htons(lport);

    fprintf(stderr, "Connecting to %s%s%s (%s)...\n", ssh ? user.c_str() : "", ssh ? "@" : "", host.c_str(), addr2str(addr).c_str());
    if (ssh) {
      out = new SshOut(addr, host, user);
    } else {
      out = new RawOut(addr, host);
    }
    if (out->fd==-1) {
      return 1;
    }
  } else {
    out = new StdOut();
  }

  poller.Create();

  out->Add(&poller);

  if (grab_devs()) {
    fprintf(stderr, "Sender failed to grab devices\n");
    return 1;
  }

  poller.Run();

  poller.Destroy();

  return 0;
}
