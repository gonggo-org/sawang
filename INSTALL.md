# Installing Sawang on Linux:

## Build Dependency

Sawang depends on cJSON shared library. Please refer to (cJSON build)[https://github.com/gonggo-org/gonggo/blob/main/INSTALL.md].

## Build Sawang

Sawang is a shared library for Gonggo proxy development. It provides a set of header file under sawang sub directory and a shared object **libsawang.so**.

1. Type `autoreconf -i` to remake the GNU Build System files in specified DIRECTORIES and their subdirectories.

2. Type `./configure` to create the configuration. A list of configure options is printed by running `configure --help`.

   To force the installation location to a specific folder, e.g. **/usr/lib64** for the shared object and **/usr/include** for the headers:
   
   ```console
   ./configure --libdir=/usr/lib64 --includedir=/usr/include
   ```

3. Type `make` to build the Sawang library.

4. Then type `sudo make install` to install the Sawang library into headers and shared object directories.
