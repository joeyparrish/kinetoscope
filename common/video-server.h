// Kinetoscope: A Sega Genesis Video Player
//
// Copyright (c) 2024 Joey Parrish
//
// See MIT License in LICENSE.txt

// Shared video server definitions.

// NOTE: This must be a plain HTTP server.  HTTPS is too expensive for this
// application and microcontroller.  Even though we could do it, it would hurt
// our slim throughput margins too much.

// Enable this to serve video from a local webserver on a hotspot.  Useful for
// demos without a solid internet connection.
//#define SERVE_FROM_HOTSPOT

#if defined(SERVE_FROM_HOTSPOT)
# define VIDEO_SERVER "192.168.84.42"
# define VIDEO_SERVER_PORT 8080
# define VIDEO_SERVER_BASE_PATH "/"
#else
# define VIDEO_SERVER "storage.googleapis.com"
# define VIDEO_SERVER_PORT 80
# define VIDEO_SERVER_BASE_PATH "/sega-kinetoscope/canned-videos/"
#endif

#define VIDEO_CATALOG_FILENAME "catalog.bin"
#define VIDEO_CATALOG_PATH VIDEO_SERVER_BASE_PATH VIDEO_CATALOG_FILENAME

#define _STRINGIFY(X) #X
#define STRINGIFY(X) _STRINGIFY(X)
#define VIDEO_SERVER_BASE_URL "http://" VIDEO_SERVER ":" STRINGIFY(VIDEO_SERVER_PORT) VIDEO_SERVER_BASE_PATH
#define VIDEO_SERVER_CATALOG_URL VIDEO_SERVER_BASE_URL VIDEO_CATALOG_FILENAME
