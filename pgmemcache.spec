Name:           pgmemcache%{?rpm_name_suffix}
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
%setup -q -n pgmemcache

%build
make

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}
echo "$(pg_config --sharedir)/extension/pgmemcache*" > files.txt
echo "$(pg_config --pkglibdir)/pgmemcache.so" >> files.txt

%clean
rm -rf %{buildroot}

%files -f files.txt
%defattr(-,root,root,-)
%doc README.rst NEWS LICENSE

%changelog
* Mon Aug 19 2013 Oskari Saarenmaa <os@ohmu.fi> - 2.1.1-11.g4e63c8a
- Initial.
