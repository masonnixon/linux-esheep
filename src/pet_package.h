#ifndef ESHEEP_PET_PACKAGE_H
#define ESHEEP_PET_PACKAGE_H

#include <glib.h>

typedef struct EsheepPetPackage EsheepPetPackage;

/* Parse and validate an eSheep behavior XML file. The returned package owns
 * every string and table it contains; it is not active until explicitly
 * installed. */
gboolean esheep_pet_package_load(const char *path, EsheepPetPackage **out,
                                 GError **error);

/* Make a validated package the process-wide animation data source. Only call
 * this before creating actors or GTK windows. */
void esheep_pet_package_activate(EsheepPetPackage *package);

/* Restore generated data and release a package. The package must not be
 * active while actors are using it. */
void esheep_pet_package_free(EsheepPetPackage *package);

#endif /* ESHEEP_PET_PACKAGE_H */
