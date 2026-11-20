#ifndef _tgl_features_h_
#define _tgl_features_h_

/* spHalfOS port: render directly in 32-bit XRGB so the framebuffer can be
   handed to SYS_WIN_PRESENT without any pixel conversion. */

#define TGL_FEATURE_ARRAYS         1
#define TGL_FEATURE_DISPLAYLISTS   1
#define TGL_FEATURE_POLYGON_OFFSET 1

/* Only the 32-bit external format is needed on spHalfOS. */
#define TGL_FEATURE_32_BITS        1

#define TGL_FEATURE_RENDER_BITS    32

#endif /* _tgl_features_h_ */
