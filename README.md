<p align="center">
  <a href="http://axiodl.github.io/boo/mascot_big.png">
    <img src="http://axiodl.github.io/boo/mascot.png" alt="Boo Mascot" width="256" height="256"/><br><em>Charlie</em>
  </a>
</p>

### Boo

**Boo** is a cross-platform windowing and event manager similar to
SDL or SFML, with additional 3D rendering functionality. 

The primary focus of Boo is 2D/3D rendering using polygon-rasterization
APIs like OpenGL or Direct3D. It exposes a unified command-queue API for 
calling the underlying graphics API.

The only per-platform responsibility of the client code is providing the 
shaders' source. Drawing, resource-management and state-switching are
performed using the unified API; these may be written once for all platforms.

Boo also features a unified audio API for mixing voices and performing variable 
sample-rate-conversion. All audio computation occurs on the CPU, synchronously 'pumped' 
by the client each frame-iteration.

Client code is entered via the `appMain` method supplied in a callback object.
This code executes on a dedicated thread with graphics command context available.
The API may be used to synchronize loops on the client thread with the display
refresh-rate.

#### Supported Graphics Backends

* OpenGL 3.3+
* Direct3D 11/12
* Metal 1.1 (OS X 10.11 only for now, iOS coming soon)
* Vulkan

#### Supported Audio Backends

* **[Windows]** WASAPI
* **[OS X]** Audio Queue Services
* **[Linux]** ALSA
