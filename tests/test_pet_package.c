#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/animations_data.h"
#include "../src/pet_package.h"

static const char PACKAGE_XML[] =
    "<animations xmlns=\"https://esheep.petrucci.ch/\">"
    "<header><tilesx>4</tilesx><tilesy>4</tilesy></header>"
    "<image><file>custom.png</file></image>"
    "<spawns><spawn id=\"1\" probability=\"100\"><x>10</x><y>20</y>"
    "<next>1</next></spawn></spawns>"
    "<animations>"
    "<animation id=\"1\"><name>custom walk</name><start><x>-1</x><y>0</y>"
    "<interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
    "<end><x>-1</x><y>0</y><interval>100</interval><offsety>0</offsety>"
    "<opacity>1</opacity></end><sequence repeat=\"0\"><frame>1</frame>"
    "<frame>2</frame><next probability=\"100\">2</next><action>flip</action></sequence>"
    "</animation>"
    "<animation id=\"2\"><name>custom idle</name><start><x>0</x><y>0</y>"
    "<interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
    "<end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety>"
    "<opacity>1</opacity></end><sequence repeat=\"0\"><frame>3</frame>"
    "<next probability=\"100\">1</next></sequence></animation>"
    "</animations>"
    "<childs><child animationid=\"1\"><x>0</x><y>0</y><next>2</next></child>"
    "<child animationid=\"1\"><x>1</x><y>0</y><next>2</next></child></childs>"
    "</animations>";

int main(void) {
    const char *path = "/tmp/esheep-test-package.xml";
    GError *error = NULL;
    EsheepPetPackage *package = NULL;
    assert(g_file_set_contents(path, PACKAGE_XML, -1, &error));
    assert(error == NULL);
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);
    assert(error == NULL);

    esheep_pet_package_activate(package);
    assert(esheep_tiles_x == 4 && esheep_tiles_y == 4);
    assert(esheep_animation_count == 2);
    assert(strcmp(esheep_animations[0].name, "custom walk") == 0);
    assert(esheep_animations[0].frame_count == 2);
    assert(esheep_animations[0].sequence_next_count == 1);
    assert(esheep_animations[0].sequence_next[0].target == 2);
    assert(esheep_animations[0].flip == 1);
    assert(esheep_spawn_count == 1 && esheep_spawns[0].x != NULL);
    assert(esheep_child_count == 2);
    assert(g_str_has_suffix(esheep_pet_package_spritesheet(package),
                            "/custom.png"));

    esheep_pet_package_free(package);
    assert(esheep_animation_count == esheep_default_animation_count);
    assert(esheep_tiles_x == esheep_default_tiles_x);
    remove(path);

    /* The shipped authored source must also pass the runtime parser, not
     * merely the small fixture above. */
    package = NULL;
    assert(esheep_pet_package_load("tools/esheep_animations.xml", &package,
                                  &error));
    assert(package != NULL && error == NULL);
    esheep_pet_package_free(package);

    package = NULL;
    const char *invalid_path = "/tmp/esheep-test-invalid-package.xml";
    const char *invalid_xml =
        "<animations><header><tilesx>1</tilesx><tilesy>1</tilesy></header>"
        "<animations><animation id=\"1\"><sequence><frame>1</frame>"
        "<next probability=\"100\">2</next></sequence></animation>"
        "</animations></animations>";
    assert(g_file_set_contents(invalid_path, invalid_xml, -1, &error));
    assert(!esheep_pet_package_load(invalid_path, &package, &error));
    assert(package == NULL);
    assert(error != NULL);
    g_clear_error(&error);
    remove(invalid_path);

    puts("All pet package tests passed");
    return 0;
}
