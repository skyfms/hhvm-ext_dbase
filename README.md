## dBase Extension for HHVM (HipHop virtual machine)

This is a port from PECL dbase extension for [HipHop PHP VM][fb-hphp], with
some minor changes, which provides dBase database file access functions.

These functions allow you to access records stored 
in dBase-format (dbf) databases.

There is no support for indexes or memo fields. 
There is no support for locking, too. 
Two concurrent webserver processes modifying the 
same dBase file will very likely ruin your database.

dBase files are simple sequential files of fixed length records. 
Records are appended to the end of the file and delete records 
are kept until you call `dbase_pack()`.

### Building & Installation

Installation requires HHVM version 3.2.0 or later. On Gentoo systems with 
[our overlay](https://github.com/skyfms/portage-overlay) you can just 
`emerge dev-php/hhvm-ext_dbase`. On other systems:

~~~
$ cd /path/to/extension
$ ./build.sh
~~~

This will produce a `dbase.so` file, the dynamically-loadable extension.

To enable the extension, you need to have the following section in your HHVM
config file (php.ini style config):

~~~
hhvm.dynamic_extension_path = /path/to/hhvm/extensions
hhvm.dynamic_extensions[dbase] = dbase.so
~~~

Where `/path/to/hhvm/extensions` is a folder containing all HipHop extensions,
and `dbase.so` is in it. This will cause the extension to be loaded when the
virtual machine starts up.

As always, bugs should be reported to the issue tracker and patches are very
welcome.

[fb-hphp]: https://github.com/facebook/hhvm "HipHop PHP"
[fb-wiki]: https://github.com/facebook/hhvm/wiki "HipHop Wiki"

