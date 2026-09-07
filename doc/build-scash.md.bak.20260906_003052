# RCPU BUILD NOTES

RCPU (s/atoshi/cash) is a fork of Bitcoin Core which adds a new chain option to restore home computer mining.

Technical details are documented in the [RCPU Protocol spec](https://github.com/rcpu-project/sips/blob/main/rcpu-protocol-spec.md).

Building RCPU follows the same instructions as building Bitcoin. The RCPU network shares the same features and rules as Bitcoin mainnet, as specified in Bitcoin Core v26.0.

The Linux version of the node `rcpud` and GUI app `rcpu-qt` are both supported, with Windows binaries also  available (cross-compiled on Linux). MacOS is not yet supported. Note that Windows users can build from source by following the Linux instructions when building in Ubuntu on [Windows Subsystem for Linux (WSL)](https://learn.microsoft.com/en-us/windows/wsl/about).

For more specific instructions on building, see [`build-unix.md`](build-unix.md) in this directory.

Also see the latest [RCPU release notes](release-notes/rcpu/).

## Getting started 

Update your system and install the following tools required to build software.

```bash
sudo apt update
sudo apt upgrade
sudo apt install build-essential libtool autotools-dev automake pkg-config bsdmainutils curl git cmake bison
```

## WSL for Windows

Ignore this step if building on native Linux. The following only applies when building in WSL for Windows.

Open the WSL configuration file:
```bash
sudo nano /etc/wsl.conf
```
Add the following lines to the file:
```
[interop]
appendWindowsPath=false
```
Exit WSL and then restart WSL.

## Downloading the code

Download the latest version of RCPU and checkout the version you intend to build. If you want to build a specific version, you can replace `rcpu_master` with the version tag.

```bash
git clone https://github.com/rcpu-project/rcpu.git
cd rcpu
git checkout rcpu_master
```

## Building for Linux

RCPU requires building with the depends system.

When calling `make` use `-j N` for N parallel jobs.

### Node software without the GUI

To build just the node software `rcpud` and not the QT GUI app:

```bash
./autogen.sh
make -C depends NO_QT=1
./configure --without-gui --prefix=$PWD/depends/x86_64-pc-linux-gnu --program-transform-name='s/bitcoin/rcpu/g'
make
make install
```

### Node software with the GUI

To build both the node software `rcpud` and the QT GUI app `rcpud-qt`

```bash
./autogen.sh
make -C depends
./configure --prefix=$PWD/depends/x86_64-pc-linux-gnu --program-transform-name='s/bitcoin/rcpu/g'
make
make install
```

### Executables
The compiled executables will be found in `depends/x86_64-pc-linux-gnu/bin/` and can be copied to a folder on your path, typically `/usr/local/bin/` or `$HOME/.local/bin/`.


## Building for Windows (by cross-compiling on Linux)

Build on Linux and generate executables which run on Windows.

```
sudo apt install g++-mingw-w64-x86-64-posix 
cd depends/
make HOST=x86_64-w64-mingw32
cd ..
./autogen.sh
./configure --prefix=$PWD/depends/x86_64-w64-mingw32 --program-transform-name='s/bitcoin/rcpu/g'
make
make install
```

The windows executables will be found in `depends/x86_64-w64-mingw32/bin/`.

To generate a Windows installer:

```
sudo apt install nsis
make deploy
```

## Config file

The RCPU configuration file is the same as bitcoin.conf.

By default, RCPU looks for a configuration file here:
`$HOME/.rcpu/rcpu.conf`

The following is a sample `rcpu.conf`.
```
rpcuser=user
rpcpassword=password
chain=rcpu
daemon=1
debug=1
txindex=1

[rcpu]
adddnsseed=seed.rcpu.one

[rcputestnet]
adddnsseed=testnet.seed.rcpu.one
```

### Connecting to the network

To help find other nodes on the network, a [DNS seed](https://bitcoin.stackexchange.com/questions/14371/what-is-a-dns-seed-node-vs-a-seed-node) has been specified. The DNS seed shown above is for testing purposes and may not always be online. Users are advised to ask the community for a list of [reliable DNS seeds](https://github.com/bitcoin/bitcoin/blob/master/doc/dnsseed-policy.md) to use, as well as the IP addresses of stable nodes on the network which can be used with the `-addnode` and `-seednode` RPC calls.

If you intend to use the same configuration file with multiple networks, the config sections are named as follows:
```
[btc]
[btctestnet3]
[btcsignet]
[btcregtest]
[rcpu]
[rcpuregtest]
[rcputestnet]
```

## Running a node

To run the RCPU node:
```bash
rcpud
```

To send commands to the RCPU node:
```
rcpu-cli [COMMAND] [PARAMETERS]
```

To run the desktop GUI app:
```bash
rcpu-qt
```

On WSL for Windows, launching `rcpu-qt` may require installing the following dependencies. Also see [WSL gui apps](https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps).
```bash
sudo apt install libxcb-* libxkbcommon-x11-0
```

Also note that in WSL for Windows, by default only half of the memory is available to WSL. You can [configure the memory limit](https://learn.microsoft.com/en-us/windows/wsl/wsl-config#main-wsl-settings) by creating `.wslconfig` file in your user folder.
```
[wsl2]
memory=16GB
```

## Connecting to different chains

When running executables with the name `bitcoin...` if no chain is configured, the default chain will be Bitcoin mainnet.

When running executables with the name `rcpu...` if no chain is configured, the default chain will be RCPU mainnet.

Option `-chain=` accepts the following values: `rcpu` `rcputestnet` `rcpuregtest` and for Bitcoin networks: `main` `test` `signet` `regtest`

## Mining RCPU

There are a few ways to mine RCPU.

### Testnet and Regtest chain

Mining takes place inside the RCPU node, using the RPC `generatetoaddress` which is single-threaded. For example:
```bash
rcpu-cli createwallet myfirstwallet
rcpu-cli getnewaddress
rcpu-cli generatetoaddress 1 newminingaddress 10000
```

To speed up mining in the RCPU node, at the expense of using more memory (at least 2GB more), enable the option `randomxfastmode` by adding to the `rcpu.conf` configuration file:

```
randomxfastmode=1
```

### Main network and Testnet chain

Mining takes place inside [cpuminer-rcpu](https://github.com/rcpu-project/cpuminer-rcpu) which is dedicated mining software that connects to the RCPU node and retrieves mining jobs via RPC `getblocktemplate`. The 'randomxfastmode' configuration option is not required for the RCPU node, since mining occurs inside `cpuminer-rcpu` which always runs in fast mode.

### Mining Pools

Third-party software exists for mining at pools.


Getting Help
---------------------

Please file a Github issue if build problems are not resolved after reviewing the available RCPU and Bitcoin documentation.
