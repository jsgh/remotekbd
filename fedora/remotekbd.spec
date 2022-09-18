Name: remotekbd
Version: 0.1
Release: 1%{?dist}
Summary: Remote keyboard/mouse utility
License: GPL
URL: https://github.com/jsgh/remotekbd
Source0: https://github.com/jsgh/remotekbd/archive/refs/tags/v%{version}.tar.gz#/%{name}-%{version}.tar.gz

%description
A small utility to allow remote keyboard and mouse (not screen) access to a remote server.

%prep
%autosetup -p1 -S git

%build
make

%install
install -D -p -m755 kbd-snd %{buildroot}/usr/bin/kbd-snd
install -p -m755 kbd-rcv %{buildroot}/usr/bin/kbd-rcv

%files
/usr/bin/kbd-snd
/usr/bin/kbd-rcv

%changelog
* Wed Aug 31 2022 John Sullivan <jsgh@kanargh.org.uk> - 0.1-1
- Initial version
