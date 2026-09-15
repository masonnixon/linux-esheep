#ifndef ESHEEP_PET_PACKAGE_H
#define ESHEEP_PET_PACKAGE_H

#include <glib.h>

typedef struct EsheepPetPackage EsheepPetPackage;
typedef struct EsheepPackageImage EsheepPackageImage;
typedef struct EsheepPackageSound EsheepPackageSound;

/* Image transparency mode as declared in the package XML. */
typedef enum {
    ESHEEP_TRANSPARENCY_NONE = 0,
    ESHEEP_TRANSPARENCY_MAGENTA,
    ESHEEP_TRANSPARENCY_TRANSPARENT,
    ESHEEP_TRANSPARENCY_GREEN,
    ESHEEP_TRANSPARENCY_CYAN,
} EsheepTransparencyMode;

/* Owned image metadata from <image> element. The package owns all strings
 * and the decoded PNG payload. */
struct EsheepPackageImage {
    int tiles_x;
    int tiles_y;
    EsheepTransparencyMode transparency;
    char *spritesheet_path;     /* from <file> or <spritesheet> */
    guchar *png_data;           /* decoded base64 from <png>, owned */
    gsize png_size;             /* size of png_data in bytes */
};

/* Owned sound record from <sound> element. The package owns the decoded
 * base64 payload. Duplicate animation IDs and source order are preserved. */
struct EsheepPackageSound {
    int animation_id;
    int probability;
    int loop_count;
    guchar *payload;            /* decoded base64, owned */
    gsize payload_size;         /* size of payload in bytes */
};

/* Parse and validate an eSheep behavior XML file. The returned package owns
 * every string, table, image metadata, and sound payload it contains; it is
 * not active until explicitly installed. */
gboolean esheep_pet_package_load(const char *path, EsheepPetPackage **out,
                                 GError **error);

/* Resolve a relative installed package path, retaining cwd-relative paths for
 * explicit command-line compatibility. The returned path must be freed. */
char *esheep_pet_package_resolve_path(const char *path, const char *data_root);

/* Make a validated package the process-wide animation data source. Only call
 * this before creating actors or GTK windows. */
void esheep_pet_package_activate(EsheepPetPackage *package);

/* Image metadata accessors. The package owns the returned data; do not free. */
const EsheepPackageImage *esheep_pet_package_image(const EsheepPetPackage *package);

/* Sound records accessors. The package owns the array and all payloads;
 * the array is ordered by source XML order and preserves duplicates. */
const EsheepPackageSound *const *esheep_pet_package_sounds(const EsheepPetPackage *package);
int esheep_pet_package_sound_count(const EsheepPetPackage *package);

/* Optional spritesheet reference from <image><file> or
 * <image><spritesheet>; the package owns the returned string. */
const char *esheep_pet_package_spritesheet(const EsheepPetPackage *package);

/* Correct a package's image grid after decoding embedded media whose
 * metadata is known to be stale. Returns FALSE if the package or grid is
 * invalid, or if any authored frame would fall outside the corrected grid. */
gboolean esheep_pet_package_set_image_grid(EsheepPetPackage *package,
                                           int tiles_x, int tiles_y);

/* Restore generated data and release a package. The package must not be
 * active while actors are using it. */
void esheep_pet_package_free(EsheepPetPackage *package);

#endif /* ESHEEP_PET_PACKAGE_H */
