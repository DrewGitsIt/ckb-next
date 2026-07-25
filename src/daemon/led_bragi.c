#include "led.h"
#include "notify.h"
#include "profile.h"
#include "usb.h"
#include "bragi_common.h"
#include "bragi_proto.h"
#include <assert.h>

// Compare two light structures, ignore keys
static inline int rgbcmp(const lighting* lhs, const lighting* rhs, const size_t zones, const size_t led_offset){
    return memcmp(lhs->r + led_offset, rhs->r + led_offset, zones) || memcmp(lhs->g + led_offset, rhs->g + led_offset, zones) || memcmp(lhs->b + led_offset, rhs->b + led_offset, zones);
}

#define LED_CASE_M(product, count) __LED_CASE(product, count, N_MOUSE_ZONES_EXTENDED)
#define LED_CASE_K(product, count) __LED_CASE(product, count, (LED_MOUSE - 1))

#define __LED_CASE(product, count, limit) case product: ; \
                                        static_assert(count <= limit, "count must be equal or less than " #limit " for dev " #product); \
                                        return count

#define CPY_SZ(colour) (sizeof(*(newlight->colour)) * zones)
static inline size_t bragi_led_count(usbdevice* kb){
    if(kb->vendor != V_CORSAIR){
        ckb_err("Vendor is not V_CORSAIR");
        return 0;
    }
    switch(kb->product){
    LED_CASE_M(P_IRONCLAW_W_U, 6);
    LED_CASE_M(P_HARPOON_WL_U, 2);
    LED_CASE_K(P_K95_PLATINUM_XT, 156);
    LED_CASE_K(P_K57_U, 137);
    LED_CASE_K(P_K60_PRO_RGB, 123);
    LED_CASE_K(P_K60_PRO_RGB_LP, 123);
    LED_CASE_K(P_K60_PRO_RGB_SE, 123);
    LED_CASE_K(P_K60_PRO_MONO, 123);
    LED_CASE_K(P_K60_PRO_TKL, 123);
    LED_CASE_M(P_KATAR_PRO_XT, 1);
    LED_CASE_M(P_KATAR_PRO, 1);
    LED_CASE_M(P_M55_RGB_PRO, 2);
    LED_CASE_M(P_SABRE_RGB_PRO, 3);
    LED_CASE_K(P_K55_PRO, 6);
    LED_CASE_K(P_K55_PRO_XT, 137);
    LED_CASE_M(P_DARK_CORE_RGB_PRO, 12);
    LED_CASE_M(P_DARK_CORE_RGB_PRO_SE, 12);
    LED_CASE_K(P_K100_OPTICAL, 193);
    LED_CASE_K(P_K100_MECHANICAL, 193);
    LED_CASE_K(P_K100_OPTICAL_VARIANT, 193);
    LED_CASE_K(P_K65_MINI, 123);
    LED_CASE_K(P_K70_TKL, 193);
    LED_CASE_K(P_K70_TKL_CHAMP_OPTIC, 193);
    LED_CASE_K(P_MM700, 3);
    LED_CASE_K(P_K70_PRO, 193);
    LED_CASE_K(P_K70_PRO_OPTIC, 193);
    LED_CASE_K(P_K70_CORE_RGB, 123);
    LED_CASE_K(P_K70_CORE_RGB_2, 123);
    LED_CASE_K(P_K70_CORE_RGB_3, 123);
    LED_CASE_M(P_SCIMITAR_ELITE_BRAGI, 5);

    default:
        ckb_err("Unknown product 0x%hx", kb->product);
        return 0;
    }
}

static int updatergb_bragi(usbdevice* kb, int force, const size_t led_offset){
    if(!kb->active)
        return 0;
    lighting* lastlight = &kb->profile->lastlight;
    lighting* newlight = &kb->profile->currentmode->light;

    // Ideally this will be moved to the usbdevice struct at some point
    const size_t zones = bragi_led_count(kb);

    // Don't do anything if the lighting hasn't changed
    if(!force && !lastlight->forceupdate && !newlight->forceupdate
            && !rgbcmp(lastlight, newlight, zones, led_offset))
        return 0;

    uchar pkt[BRAGI_JUMBO_SIZE] = {0};

    // Since the blank pkt is used to check if the lights are off, we need to make sure it's sufficiently large
    static_assert(LED_MOUSE <= sizeof(pkt), "pkt is not large enough to check if all zones are off");
    // Switch LEDs off if its all black, because being able to just switch them off even in hw mode is really nice
    int newon  = memcmp( newlight->r + led_offset, pkt, CPY_SZ(r)) ||
                 memcmp( newlight->g + led_offset, pkt, CPY_SZ(g)) ||
                 memcmp( newlight->b + led_offset, pkt, CPY_SZ(b));
    int laston = memcmp(lastlight->r + led_offset, pkt, CPY_SZ(r)) ||
                 memcmp(lastlight->g + led_offset, pkt, CPY_SZ(g)) ||
                 memcmp(lastlight->b + led_offset, pkt, CPY_SZ(b));

    static_assert(sizeof(pkt) >= 7 + N_KEYS_EXTENDED * 3, "Bragi RGB packet must be large enough to fit all possible zones in the keymap");

    size_t bytes = zones;

    memcpy(pkt + 7, newlight->r + led_offset, CPY_SZ(r));
    if(!IS_MONOCHROME_DEV(kb)) {
        bytes *= 3; // 3 channels
        memcpy(pkt + 7 + CPY_SZ(r), newlight->g + led_offset, CPY_SZ(g));
        memcpy(pkt + 7 + CPY_SZ(r) + CPY_SZ(g), newlight->b + led_offset, CPY_SZ(b));
    }

    if(bragi_write_to_handle(kb, pkt, BRAGI_LIGHTING_HANDLE, sizeof(pkt), bytes))
        return 1;

    // Keep this check below the write.
    // This is done to prevent a delay when turning the lights off, caused by slow HW.
    // There seems to be no way to prevent the delay when turning the lights back on.
    if (newon != laston || force){
        if(kb->brightness_mode == BRIGHTNESS_HARDWARE_COARSE)
            bragi_set_property(kb, BRAGI_BRIGHTNESS_COARSE, newon ? 3 : 0);
        else if(kb->brightness_mode == BRIGHTNESS_HARDWARE_FINE)
            bragi_set_property(kb, BRAGI_BRIGHTNESS, newon ? 1000 : 0);
    }

    lastlight->forceupdate = newlight->forceupdate = 0;

    memcpy(lastlight, newlight, sizeof(lighting));
    return 0;
}

int updatergb_mouse_bragi(usbdevice* kb, int force){
    return updatergb_bragi(kb, force, LED_MOUSE);
}

int updatergb_keyboard_bragi(usbdevice* kb, int force){
    return updatergb_bragi(kb, force, 0);
}

#define BRAGI_ALT_RGB_HEADER 2
static inline int updatergb_alt_bragi(usbdevice* kb, int force){
    if(!kb->active)
        return 0;
    lighting* lastlight = &kb->profile->lastlight;
    lighting* newlight = &kb->profile->currentmode->light;

    // Ideally this will be moved to the usbdevice struct at some point
    const size_t zones = bragi_led_count(kb);

    // Don't do anything if the lighting hasn't changed
    if(!force && !lastlight->forceupdate && !newlight->forceupdate
            && !rgbcmp(lastlight, newlight, zones, 0))
        return 0;

    uchar pkt1[BRAGI_JUMBO_SIZE] = {0};
    pkt1[7] = 0x12; // Some kind of header?

    uchar* start = pkt1 + 7 + BRAGI_ALT_RGB_HEADER;
    // Copy red first
    for(size_t i = 0; i < zones; i++)
        start[i * 3] = newlight->r[i];

    // Green
    for(size_t i = 0; i < zones; i++)
        start[i * 3 + 1] = newlight->g[i];

    // Blue
    for(size_t i = 0; i < zones; i++)
        start[i * 3 + 2] = newlight->b[i];

    if(bragi_write_to_handle(kb, pkt1, BRAGI_LIGHTING_HANDLE, sizeof(pkt1), 3 * zones + BRAGI_ALT_RGB_HEADER))
        return 1;

    lastlight->forceupdate = newlight->forceupdate = 0;

    memcpy(lastlight, newlight, sizeof(lighting));
    return 0;
}

int updatergb_keyboard_bragi_alt(usbdevice* kb, int force){
    return updatergb_alt_bragi(kb, force);
}

// The SABRE RGB PRO is planar (R plane, then G, then B) like the generic Bragi path,
// but at a fixed 5-byte stride instead of that path's tight zone-count stride: handle
// 0x00 expects each plane at offsets 0/5/10, holding logo, wheel, and 3 dpi indicator LEDs
// add up to a 15-byte data length. See how we use the dpi LEDs below:
//     [Rlogo Rwheel Rdpi1 Rdpi2 Rdpi3][G...][B...]
// Color index order (LED_MOUSE + i) matches the plane order: 0 = logo, 1 = wheel, 2 = dpi.
#define SABRE_PLANE_STRIDE  5

// Paint the SABRE's 3-LED DPI level meter into the packet's frame positions 2,3,4.
// The bar shows the current DPI stage's position (x-- / xx- / -x- / -xx / --x) tinted by
// that stage's own colour (DPI_RGB_START + stage). In hardware mode the firmware drives
// these positionally; in software mode we must render them, or they collapse to a single
// lit LED that merely changes colour. `start` points at the first plane; `stage` is
// dpi.current, which runs 1..5 (0 is only the power-on default, never part of the cycle).
static void sabre_paint_dpi_bar(uchar* start, const lighting* newlight, uchar stage){
    static const uchar dpi_bar[5][3] = {
        {1,0,0}, {1,1,0}, {0,1,0}, {0,1,1}, {0,0,1}
    };
    // Map the live stage onto the 5 pattern rows (0..4) - i.e. one less than dpi.current.
    int pat = (int)stage - 1;
    if(pat < 0)
        pat = 0;
    else if(pat > 4)
        pat = 4;
    // Colour comes from the DPI stage's own colour slot, indexed by the real stage.
    uchar cstage = stage > 5 ? 5 : stage;
    uchar cr = newlight->r[DPI_RGB_START + cstage];
    uchar cg = newlight->g[DPI_RGB_START + cstage];
    uchar cb = newlight->b[DPI_RGB_START + cstage];
    // Frame positions 2,3,4 run physically right-to-left, so the leftmost pattern bit maps
    // to the highest frame position (4-j) to match how the bar is seen on the mouse.
    for(int j = 0; j < 3; j++){
        uchar on = dpi_bar[pat][j];
        start[0 * SABRE_PLANE_STRIDE + (4 - j)] = on ? cr : 0;
        start[1 * SABRE_PLANE_STRIDE + (4 - j)] = on ? cg : 0;
        start[2 * SABRE_PLANE_STRIDE + (4 - j)] = on ? cb : 0;
    }
}

int updatergb_sabre_pro_bragi(usbdevice* kb, int force){
    if(!kb->active)
        return 0;
    lighting* lastlight = &kb->profile->lastlight;
    lighting* newlight = &kb->profile->currentmode->light;

    // One source of truth for the zone count (see bragi_led_count: LED_CASE_M(P_SABRE_RGB_PRO, 3))
    const size_t zones = bragi_led_count(kb);

    // Shortcut if the lighting hasn't changed
    if(!force && !lastlight->forceupdate && !newlight->forceupdate
            && !rgbcmp(lastlight, newlight, zones, LED_MOUSE)
            && !rgbcmp(lastlight, newlight, DPI_RGB_COUNT, DPI_RGB_START))
        return 0;

    uchar pkt[BRAGI_JUMBO_SIZE] = {0};
    static_assert(sizeof(pkt) >= 7 + 3 * SABRE_PLANE_STRIDE, "Bragi RGB packet must be large enough for the SABRE layout");

    // These checks compare against the still-blank pkt as an all-zero reference. 
    // Switches LEDs off if it's all black so hw mode can turn them off entirely.
    int newon  = memcmp(newlight->r + LED_MOUSE, pkt, zones) ||
                 memcmp(newlight->g + LED_MOUSE, pkt, zones) ||
                 memcmp(newlight->b + LED_MOUSE, pkt, zones);
    int laston = memcmp(lastlight->r + LED_MOUSE, pkt, zones) ||
                 memcmp(lastlight->g + LED_MOUSE, pkt, zones) ||
                 memcmp(lastlight->b + LED_MOUSE, pkt, zones);

    uchar* start = pkt + 7;
    for(size_t i = 0; i < zones; i++){
        start[0 * SABRE_PLANE_STRIDE + i] = newlight->r[LED_MOUSE + i];  // red plane
        start[1 * SABRE_PLANE_STRIDE + i] = newlight->g[LED_MOUSE + i];  // green plane
        start[2 * SABRE_PLANE_STRIDE + i] = newlight->b[LED_MOUSE + i];  // blue plane
    }

    // Overlay the software-mode DPI level meter on frame positions 2,3,4.
    sabre_paint_dpi_bar(start, newlight, kb->profile->currentmode->dpi.current);

    if(bragi_write_to_handle(kb, pkt, BRAGI_LIGHTING_HANDLE, sizeof(pkt), 3 * SABRE_PLANE_STRIDE))
        return 1;

    // Keep this check below the write to avoid a delay when turning the lights off.
    if(newon != laston || force){
        if(kb->brightness_mode == BRIGHTNESS_HARDWARE_COARSE)
            bragi_set_property(kb, BRAGI_BRIGHTNESS_COARSE, newon ? 3 : 0);
        else if(kb->brightness_mode == BRIGHTNESS_HARDWARE_FINE)
            bragi_set_property(kb, BRAGI_BRIGHTNESS, newon ? 1000 : 0);
    }

    lastlight->forceupdate = newlight->forceupdate = 0;

    memcpy(lastlight, newlight, sizeof(lighting));
    return 0;
}
