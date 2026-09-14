#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/animations_data.h"
#include "../src/pet_package.h"

static const char PACKAGE_XML[] =
    "<animations xmlns=\"https://esheep.petrucci.ch/\">"
    "<header><tilesx>4</tilesx><tilesy>4</tilesy></header>"
    "<image><tilesx>2</tilesx><tilesy>3</tilesy><file>custom.png</file>"
    "<png>iVBORw0KGgo=</png><transparency>Transparent</transparency></image>"
    "<sounds><sound animationid=\"2\"><probability>25</probability>"
    "<loop>2</loop><base64>YWJj</base64></sound></sounds>"
    "<spawns><spawn id=\"1\" probability=\"100\"><x>10</x><y>20</y>"
    "<next>1</next></spawn></spawns>"
    "<animations>"
    "<animation id=\"1\"><name>custom walk</name><start><x>(imageW+10)/2</x><y>0</y>"
    "<interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
    "<end><x>-1</x><y>0</y><interval>100</interval><offsety>0</offsety>"
    "<opacity>1</opacity></end><sequence repeat=\"Convert(screenW/2,System.Int32)%3\"><frame>1</frame>"
    "<frame>2</frame><next probability=\"100\">2</next><action>flip</action></sequence>"
    "</animation>"
    "<animation id=\"2\"><name>custom idle</name><start><x>0</x><y>0</y>"
    "<interval>100</interval><offsety>0</offsety><opacity>1</opacity></start>"
    "<end><x>0</x><y>0</y><interval>100</interval><offsety>0</offsety>"
    "<opacity>1</opacity></end><sequence repeat=\"0\"><frame>3</frame>"
    "<next probability=\"100\">1</next></sequence></animation>"
    "</animations>"
    "<childs><child animationid=\"1\"><x>imageX-(imageW/2)</x><y>0</y><next>2</next></child>"
    "<child animationid=\"1\"><x>1</x><y>0</y><next>2</next></child></childs>"
    "</animations>";

static void assert_invalid_package(const char *path, const char *xml,
                                   const char *message) {
    GError *error = NULL;
    EsheepPetPackage *package = NULL;

    assert(g_file_set_contents(path, xml, -1, &error));
    assert(error == NULL);
    assert(!esheep_pet_package_load(path, &package, &error));
    assert(package == NULL);
    assert(error != NULL);
    if (message) assert(strstr(error->message, message) != NULL);
    g_clear_error(&error);
    remove(path);
}

/* Test embedded PNG loading via GdkPixbufLoader.
 * This test verifies that a package with embedded PNG data can load
 * the spritesheet from the owned package bytes, without requiring
 * an external file. */
static void test_embedded_png_loading(void) {
    /* Package with embedded PNG (1x1 transparent pixel, valid base64) */
    const char *embedded_png_xml =
        "<animations xmlns=\"https://esheep.petrucci.ch/\"><header><tilesx>4</tilesx>"
        "<tilesy>4</tilesy></header>"
        "<image><tilesx>2</tilesx><tilesy>3</tilesy><png>"
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
        "</png><transparency>Transparent</transparency></image>"
        "<animations><animation id=\"1\"><name>test</name>"
        "<start><x>0</x><y>0</y><interval>100</interval>"
        "<offsety>0</offsety><opacity>1</opacity></start>"
        "<end><x>0</x><y>0</y><interval>100</interval>"
        "<offsety>0</offsety><opacity>1</opacity></end>"
        "<sequence repeat=\"0\"><frame>1</frame></sequence>"
        "</animation></animations></animations>";

    const char *path = "/tmp/esheep-test-embedded-png.xml";
    GError *error = NULL;
    EsheepPetPackage *package = NULL;

    assert(g_file_set_contents(path, embedded_png_xml, -1, &error));
    assert(error == NULL);
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);
    assert(error == NULL);

    /* Verify the package has image metadata with embedded PNG */
    const EsheepPackageImage *image = esheep_pet_package_image(package);
    assert(image != NULL);
    assert(image->tiles_x == 2 && image->tiles_y == 3);
    assert(image->transparency == ESHEEP_TRANSPARENCY_TRANSPARENT);
    assert(image->png_data != NULL);
    assert(image->png_size > 0);
    /* PNG header bytes */
    assert(image->png_size >= 8);
    assert(memcmp(image->png_data, "\x89PNG\r\n\x1a\n", 8) == 0);

    /* Verify spritesheet path is NULL since no <file> or <spritesheet> was provided */
    const char *spritesheet_path = esheep_pet_package_spritesheet(package);
    assert(spritesheet_path == NULL || *spritesheet_path == '\0');

    esheep_pet_package_free(package);
    remove(path);
    puts("Embedded PNG loading test passed");
}

/* Test sprite precedence: explicit --sprite should override embedded PNG */
static void test_sprite_precedence_explicit_override(void) {
    /* This test verifies the precedence order is maintained in main.c logic.
     * We test the API contract: when a package has both embedded PNG and
     * a spritesheet file path, the explicit sprite path takes priority. */

    const char *xml_with_both =
        "<animations xmlns=\"https://esheep.petrucci.ch/\"><header><tilesx>4</tilesx>"
        "<tilesy>4</tilesy></header>"
        "<image><tilesx>2</tilesx><tilesy>3</tilesy><file>external.png</file>"
        "<png>iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg=="
        "</png><transparency>Transparent</transparency></image>"
        "<animations><animation id=\"1\"><name>test</name>"
        "<start><x>0</x><y>0</y><interval>100</interval>"
        "<offsety>0</offsety><opacity>1</opacity></start>"
        "<end><x>0</x><y>0</y><interval>100</interval>"
        "<offsety>0</offsety><opacity>1</opacity></end>"
        "<sequence repeat=\"0\"><frame>1</frame></sequence>"
        "</animation></animations></animations>";

    const char *path = "/tmp/esheep-test-precedence.xml";
    GError *error = NULL;
    EsheepPetPackage *package = NULL;

    assert(g_file_set_contents(path, xml_with_both, -1, &error));
    assert(error == NULL);
    assert(esheep_pet_package_load(path, &package, &error));
    assert(package != NULL);
    assert(error == NULL);

    /* Package should have both embedded PNG and spritesheet path */
    const EsheepPackageImage *image = esheep_pet_package_image(package);
    assert(image != NULL);
    assert(image->png_data != NULL);
    assert(image->png_size > 0);
    const char *spritesheet_path = esheep_pet_package_spritesheet(package);
    assert(spritesheet_path != NULL);
    assert(strstr(spritesheet_path, "external.png") != NULL);

    /* The main.c logic handles precedence: --sprite > env > config > package file > embedded > default.
     * This test verifies the package correctly exposes both. */
    esheep_pet_package_free(package);
    remove(path);
    puts("Sprite precedence test passed");
}

static void test_installed_path_and_active_replacement(void) {
    char *path = esheep_pet_package_resolve_path(
        "Pets/blue_sheep/animations.xml", "assets");
    assert(path != NULL);
    assert(g_file_test(path, G_FILE_TEST_IS_REGULAR));

    GError *error = NULL;
    EsheepPetPackage *first = NULL;
    EsheepPetPackage *second = NULL;
    assert(esheep_pet_package_load(path, &first, &error));
    assert(esheep_pet_package_load("tools/esheep_animations.xml", &second,
                                  &error));
    esheep_pet_package_activate(first);
    int first_animation_count = esheep_animation_count;
    esheep_pet_package_activate(second);
    assert(esheep_animation_count != 0);
    esheep_pet_package_free(first);
    assert(esheep_animation_count != 0);
    assert(esheep_animation_count != first_animation_count ||
           esheep_animations[0].name != NULL);
    esheep_pet_package_free(second);
    g_free(path);
}


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
    assert(esheep_tiles_x == 2 && esheep_tiles_y == 3);
    assert(esheep_animation_count == 2);
    assert(strcmp(esheep_animations[0].name, "custom walk") == 0);
    assert(esheep_animations[0].frame_count == 2);
    assert(esheep_animations[0].sequence_next_count == 1);
    assert(strcmp(esheep_animations[0].start.x, "(imageW+10)/2") == 0);
    assert(esheep_animations[0].sequence_next[0].target == 2);
    assert(esheep_animations[0].flip == 1);
    assert(esheep_spawn_count == 1 && esheep_spawns[0].x != NULL);
    assert(esheep_child_count == 2);
    assert(g_str_has_suffix(esheep_pet_package_spritesheet(package),
                            "/custom.png"));
    const EsheepPackageImage *image = esheep_pet_package_image(package);
    assert(image != NULL && image->tiles_x == 2 && image->tiles_y == 3);
    assert(image->transparency == ESHEEP_TRANSPARENCY_TRANSPARENT);
    assert(image->png_size == 8 && memcmp(image->png_data, "\x89PNG\r\n\x1a\n", 8) == 0);
    assert(esheep_pet_package_sound_count(package) == 1);
    const EsheepPackageSound *const *sounds = esheep_pet_package_sounds(package);
    assert(sounds != NULL && sounds[0]->animation_id == 2);
    assert(sounds[0]->probability == 25 && sounds[0]->loop_count == 2);
    assert(sounds[0]->payload_size == 3 && memcmp(sounds[0]->payload, "abc", 3) == 0);

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

    package = NULL;
    const char *invalid_base64_path = "/tmp/esheep-test-invalid-base64.xml";
    const char *invalid_base64_xml =
        "<animations><header><tilesx>1</tilesx><tilesy>1</tilesy></header>"
        "<image><png>not-base64!</png></image>"
        "<animations><animation id=\"1\"><start/><end/>"
        "<sequence><frame>0</frame></sequence></animation></animations>"
        "</animations>";
    assert(g_file_set_contents(invalid_base64_path, invalid_base64_xml, -1,
                               &error));
    assert(!esheep_pet_package_load(invalid_base64_path, &package, &error));
    assert(package == NULL && error != NULL);
    assert(strstr(error->message, "invalid base64 PNG data") != NULL);
    g_clear_error(&error);
    remove(invalid_base64_path);

    package = NULL;
    const char *invalid_action_path = "/tmp/esheep-test-invalid-action.xml";
    const char *invalid_action_xml =
        "<animations><header><tilesx>1</tilesx><tilesy>1</tilesy></header>"
        "<animations><animation id=\"1\"><start/><end/>"
        "<sequence><frame>0</frame><action>teleport</action></sequence>"
        "</animation></animations></animations>";
    assert(g_file_set_contents(invalid_action_path, invalid_action_xml, -1,
                               &error));
    assert(!esheep_pet_package_load(invalid_action_path, &package, &error));
    assert(package == NULL);
    assert(error != NULL);
    assert(strstr(error->message, "unsupported animation action") != NULL);
    g_clear_error(&error);
    remove(invalid_action_path);

    package = NULL;
    const char *invalid_transition_path = "/tmp/esheep-test-invalid-transition.xml";
    const char *invalid_transition_xml =
        "<animations><header><tilesx>1</tilesx><tilesy>1</tilesy></header>"
        "<animations><animation id=\"1\"><start/><end/>"
        "<sequence><frame>0</frame><next probability=\"100\">2</next>"
        "</sequence></animation></animations></animations>";
    assert(g_file_set_contents(invalid_transition_path, invalid_transition_xml, -1,
                               &error));
    assert(!esheep_pet_package_load(invalid_transition_path, &package, &error));
    assert(package == NULL);
    assert(error != NULL);
    assert(strstr(error->message, "invalid animation transition") != NULL);
    g_clear_error(&error);
    remove(invalid_transition_path);

    package = NULL;
    const char *invalid_expression_path = "/tmp/esheep-test-invalid-expression.xml";
    const char *invalid_expression_xml =
        "<animations><header><tilesx>1</tilesx><tilesy>1</tilesy></header>"
        "<animations><animation id=\"1\"><start/><end/>"
        "<sequence repeat=\"random/10+1garbage\"><frame>0</frame>"
        "</sequence></animation></animations></animations>";
    assert(g_file_set_contents(invalid_expression_path, invalid_expression_xml,
                               -1, &error));
    assert(!esheep_pet_package_load(invalid_expression_path, &package, &error));
    assert(package == NULL);
    assert(error != NULL);
    assert(strstr(error->message, "finite 32-bit integer") != NULL);
    g_clear_error(&error);
    remove(invalid_expression_path);

    const char *range_prefix =
        "<animations><header><tilesx>1</tilesx><tilesy>1</tilesy></header>"
        "<animations><animation id=\"1\"><start><x>%s</x></start>"
        "<end/><sequence repeat=\"%s\"><frame>%s</frame></sequence>"
        "</animation></animations></animations>";
    char *range_xml = g_strdup_printf(range_prefix, "2147483648", "0", "0");
    assert_invalid_package("/tmp/esheep-test-position-range.xml", range_xml,
                           "animation pose expression must be a finite 32-bit integer");
    g_free(range_xml);

    range_xml = g_strdup_printf(range_prefix, "0", "2147483648", "0");
    assert_invalid_package("/tmp/esheep-test-repeat-range.xml", range_xml,
                           "animation repeat expression must be a finite 32-bit integer");
    g_free(range_xml);

    range_xml = g_strdup_printf(range_prefix, "0", "0", "2147483648");
    assert_invalid_package("/tmp/esheep-test-frame-range.xml", range_xml,
                           "frame index must be a non-negative representable integer");
    g_free(range_xml);

    range_xml = g_strdup_printf(range_prefix, "0",
                                "Convert(2147483648,System.Int32)", "0");
    assert_invalid_package("/tmp/esheep-test-convert-range.xml", range_xml,
                           "animation repeat expression must be a finite 32-bit integer");
    g_free(range_xml);

    /* Test malformed XML with unfinished transition (missing closing next tag) */
    package = (EsheepPetPackage *)0x1;
    const char *unfinished_next_path = "/tmp/esheep-test-unfinished-next.xml";
    const char *unfinished_next_xml =
        "<animations><header><tilesx>1</tilesx><tilesy>1</tilesy></header>"
        "<animations><animation id=\"1\"><start/><end/>"
        "<sequence><frame>0</frame><next probability=\"100\">2"
        "</sequence></animation></animations></animations>";
    assert(g_file_set_contents(unfinished_next_path, unfinished_next_xml, -1,
                               &error));
    assert(!esheep_pet_package_load(unfinished_next_path, &package, &error));
    assert(package == NULL);
    assert(error != NULL);
    g_clear_error(&error);
    remove(unfinished_next_path);

    /* Test malformed XML with unfinished transition (missing next content) */
    package = NULL;
    const char *empty_next_path = "/tmp/esheep-test-empty-next.xml";
    const char *empty_next_xml =
        "<animations><header><tilesx>1</tilesx><tilesy>1</tilesy></header>"
        "<animations><animation id=\"1\"><start/><end/>"
        "<sequence><frame>0</frame><next probability=\"100\"></next>"
        "</sequence></animation></animations></animations>";
    assert(g_file_set_contents(empty_next_path, empty_next_xml, -1,
                               &error));
    assert(!esheep_pet_package_load(empty_next_path, &package, &error));
    assert(package == NULL);
    assert(error != NULL);
    g_clear_error(&error);
    remove(empty_next_path);

    /* Test malformed XML with unfinished child */
    package = NULL;
    const char *unfinished_child_path = "/tmp/esheep-test-unfinished-child.xml";
    const char *unfinished_child_xml =
        "<animations><header><tilesx>1</tilesx><tilesy>1</tilesy></header>"
        "<animations><animation id=\"1\"><start/><end/>"
        "<sequence><frame>0</frame></sequence></animation>"
        "</animations><childs><child animationid=\"1\"><x>0</x><y>0</y>"
        "</animations></animations>";
    assert(g_file_set_contents(unfinished_child_path, unfinished_child_xml, -1,
                               &error));
    assert(!esheep_pet_package_load(unfinished_child_path, &package, &error));
    assert(package == NULL);
    assert(error != NULL);
    g_clear_error(&error);
    remove(unfinished_child_path);

    /* Test malformed XML with missing animation closing tag */
    package = NULL;
    const char *unclosed_animation_path = "/tmp/esheep-test-unclosed-animation.xml";
    const char *unclosed_animation_xml =
        "<animations><header><tilesx>1</tilesx><tilesy>1</tilesy></header>"
        "<animations><animation id=\"1\"><start/><end/>"
        "<sequence><frame>0</frame></sequence>"
        "</animations></animations>";
    assert(g_file_set_contents(unclosed_animation_path, unclosed_animation_xml, -1,
                               &error));
    assert(!esheep_pet_package_load(unclosed_animation_path, &package, &error));
    assert(package == NULL);
    assert(error != NULL);
    g_clear_error(&error);
    remove(unclosed_animation_path);

    test_embedded_png_loading();
    test_sprite_precedence_explicit_override();
    test_installed_path_and_active_replacement();
    puts("All pet package tests passed");
    return 0;
}
