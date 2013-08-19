Name:           pgmemcache
Version:        %{major_version}
Release:        %{minor_version}%{?dist}
Summary:        PostgreSQL memcache functions

Group:          Applications/Databases
License:        MIT
Source0:        pgmemcache-rpm-src.tar.gz

%description
pgmemcache is a set of PostgreSQL user-defined functions that provide an
interface to memcached.  Installing pgmemcache is easy, but does have a few
trivial requirements.

%prep
%setup -q -n %{name}

%build
make

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}

%clean
rm -rf %{buildroot}

%files
%defattr(-,root,root,-)
%doc README NEWS LICENSE
%{_libdir}/pgsql/pgmemcache.so
%{_datadir}/pgsql/extension/pgmemcache*

%changelog
* Mon Aug 19 2013 Oskari Saarenmaa <os@ohmu.fi> - 2.1.1-11.g4e63c8a
- Initial.
