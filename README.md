nxplay - media playback library based on GStreamer, version 0.9.0 (alpha)
=========================================================================

About
-----

nxplay is a C++ library for playing media by using the [GStreamer framework](http://gstreamer.freedesktop.org/).
The library provides a high level interface for players without having to worry about
low-level GLib/GStreamer specific details. In addition, it supports gapless playback
with prebuffering, and sets up its own internal GLib mainloop. This way, users can
play media without having to set up a GLib mainloop themselves.


Design overview
---------------

nxplay operates with media objects and pipeline objects (or just "pipelines"). Currently,
there is only one pipeline, `main_pipeline`. Future versions will allow for switching
between pipelines based on the media object to play.

Media objects represent media that is to be played, or is being played etc. A media
object contains a URI (as a string) and a payload (using the `any` type from any.hpp).
The payload is user defined and can be pretty much anything; nxplay does not modify
this value. Payloads are useful for associating information to a certain URI.

Pipelines accomplish gapless playback by having "current" and "next" media. Current media
is what is playing right now. Next media is what is scheduled to be played immediately
after the current media ends. The pipelines do the transition to the next media internally
and automatically.

nxplay pipelines try to be as robust as possible, by making sure that the pipelines do not
freeze, or reach an undefined state, no matter what calls may be coming in. Invalid media,
GStreamer errors, device failures etc. are handled. In case of nonrecoverable internal
GStreamer pipeline errors, pipelines reinitialize themselves.

Currently, nxplay is limited to audio playback only. However, extensions for video, subtitles
etc. are planned for future versions.


License
-------

The library is licensed under the [Boost Software License version 1.0](http://www.boost.org/LICENSE_1_0.txt).
The header `any.hpp` in the `nxplay` directory is put under the same licensed, but was
originally written by Christopher Diggins, Pablo Aguilar, and Kevlin Henney, and
published [on the codeproject page](http://www.codeproject.com/Articles/11250/High-Performance-Dynamic-Typing-in-C-using-a-Repla).


Dependencies
------------

GStreamer 1.5.2 or newer is required.
A C++11 capable compiler is also needed (GCC 4.8 and clang 3.4 should work fine).
Doxygen 1.8 or newer is needed for generating the reference documentation. If Doxygen is not available,
and no docs are needed, use the `--disable-docs` switch when configuring the build (see below).


Building and installing
-----------------------

This project uses the [waf meta build system](https://code.google.com/p/waf/). To configure, first set
the following environment variables to whatever is necessary for cross compilation for your platform:

* `CC`
* `CXX`
* `CFLAGS`
* `CXXFLAGS`
* `LDFLAGS`
* `PKG_CONFIG_PATH`
* `PKG_CONFIG_SYSROOT_DIR`

Then, run:

    ./waf configure --prefix=PREFIX

(The aforementioned environment variables are only necessary for this configure call.)
PREFIX defines the installation prefix, that is, where the built binaries will be installed.

Additional optional configuration switches are:

* `--enable-debug` : adds debug compiler flags to the build
* `--disable-docs` : turns off reference documentation generation with Doxygen

Once configuration is complete, run:

    ./waf

This builds the library.
Finally, to install, run:

    ./waf install


Using the library
-----------------

This simple example shows the basic usage of nxplay. It plays a given URI for
5 seconds, then shuts down.

    #include <thread>
    #include <chrono>
    #include <iostream>
    #include <nxplay/log.hpp>
    #include <nxplay/init_gstreamer.hpp>
    #include <nxplay/main_pipeline.hpp>
    
    int main(int argc, char *argv[])
    {
        nxplay::set_min_log_level(nxplay::log_level_trace);
        nxplay::set_stderr_output();
   
        // First, initialize GStreamer. This call is a convenience
        // wrapper for gst_init_check().
        if (!nxplay::init_gstreamer(&argc, &argv))
        {
            std::cerr << "Could not initialize GStreamer\n";
            return -1;
        }
    
        // A URI is required
        if (argc < 2)
        {
            std::cerr << "Missing URI\n";
            return -1;
        }
    
        {
            // Set up some callbacks
            nxplay::main_pipeline::callbacks cb;
            cb.m_media_started_callback = [](nxplay::media const &p_current_media, guint64 const p_token) {
                std::cout << "Media started, URI: " << p_current_media.get_uri() << std::endl;
            };
    
            // Create the pipeline
            nxplay::main_pipeline pipeline(cb);
    
            // Play the media with the URI from the command line
            pipeline.play_media(pipeline.get_new_token(), nxplay::media(argv[1]), true);
    
            // Wait for 5 seconds. Then, the pipeline's destructor will
            // automatically stop playback and cleanup the GStreamer pipeline
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    
        // Deinit GStreamer now (internally calls gst_deinit() )
        // keeping this call outside of the above scope to ensure
        // the main pipeline is destroyed first
        nxplay::deinit_gstreamer();
    
        return 0;
    }

