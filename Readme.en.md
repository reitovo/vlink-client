## VTube Studio Link!

![build](https://github.com/reitovo/vtslink-serverPeer/actions/workflows/cmake.yml/badge.svg)

### Introduction

`VTube Studio Link!` can help v-tubers who use `VTube Studio` to host a collaborative event, featuring:

1. No need to share models, no copyright worries from software that is based on model sharing like `PrprLive`.
2. Support native transparency.
3. Low system resource consumption, fully hardware accelerated.
4. Supports P2P and Relay transmission to cover all network situations.
5. No need to deploy clientPeers yourself, built-in one-key start (charged).

### [Code Walkthrough](https://www.wolai.com/reito/dGzCn2JJCB8tnZwWd6wcRN) 

### Compiling

It is a `CMake` project. GUI is based on `Qt`, use `vcpkg` to manage dependencies. 

1. Install [vcpkg](https://github.com/microsoft/vcpkg)
2. Open `CMakeLists.txt` with your favorite IDE.
3. Set build root and install root as you wish.
4. Set environment variables in IDE CMake project settings, take `CLion` as an example:

  | Name                   | Where to get one     |
  |------------------------|----------------------|
  | BACKTRACE_SUBMIT_TOKEN | https://backtrace.io |

  ![image](https://user-images.githubusercontent.com/29846655/212706928-4a4a8271-103a-4adf-a580-d8045152d7dd.png)

5. Compile

### [LICENSE](LICENSE)
