%define debug_package %{nil}

Name:           arm64-runner
Version:        1.0
Release:        0.rc2%{?dist}
Summary:        ARM64 Runner — эмулятор ARM64 ELF бинарников с поддержкой livepatch

License:        GPLv3
URL:            https://example.com/arm64-runner
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  gcc, make
Requires:       glibc

%description
ARM64 Runner RC2 — эмулятор ARM64 ELF бинарников с поддержкой livepatch.

%prep
%setup -q

%build
make all

%install
rm -rf %{buildroot}
install -D -m 0755 arm64_runner %{buildroot}/usr/bin/arm64_runner
install -D -m 0644 docs/README.md %{buildroot}/usr/share/doc/arm64-runner/README

%files
/usr/bin/arm64_runner
/usr/share/doc/arm64-runner/README

%changelog
* Thu Jun 13 2024 Evgeny Borodin <noreply@example.com> - 1.0-rc2-1
- Initial RPM release 