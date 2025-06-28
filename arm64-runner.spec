%define debug_package %{nil}

Name:           arm64-runner
Version:        1.0
Release:        1%{?dist}
Summary:        ARM64 Runner — эмулятор ARM64 ELF бинарников с поддержкой livepatch

License:        GPLv3
URL:            https://example.com/arm64-runner
Source0:        %{name}-%{version}.tar.gz
BuildRequires:  gcc, make
Requires:       glibc

%description
ARM64 Runner 1.0 — эмулятор ARM64 ELF бинарников с поддержкой livepatch.

%prep
%setup -q

%build
make arm64_runner

%install
rm -rf %{buildroot}
install -D -m 0755 arm64_runner %{buildroot}/usr/bin/arm64_runner
install -D -m 0755 update_module %{buildroot}/usr/bin/update_module
install -D -m 0755 livepatch %{buildroot}/usr/bin/livepatch
install -D -m 0755 examples/livepatch_example %{buildroot}/usr/bin/livepatch_example
install -D -m 0755 examples/security_patch_example %{buildroot}/usr/bin/security_patch_example
install -D -m 0755 examples/livepatch_security_demo %{buildroot}/usr/bin/livepatch_security_demo
install -D -m 0644 docs/README.md %{buildroot}/usr/share/doc/arm64-runner/README

%files
/usr/bin/arm64_runner
/usr/bin/update_module
/usr/bin/livepatch
/usr/bin/livepatch_example
/usr/bin/security_patch_example
/usr/bin/livepatch_security_demo
/usr/share/doc/arm64-runner/README

%changelog
* Thu Jun 13 2024 Evgeny Borodin <noreply@example.com> - 1.0-1
- Initial RPM release 