/*
 * x12-info — diagnostic tool for X12 engine
 * Run this on device to see what the engine detects.
 */
#include "x12_engine.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("X12 Adaptive Performance Engine v%s\n", X12_ENGINE_VERSION_STR);
    printf("==========================================\n");

    X12Config cfg = {0};
    cfg.package_name = "x12-info";
    cfg.pkg_type     = X12_PKG_TOOL;
    cfg.verbose      = true;

    X12Engine *eng = x12_init(&cfg);
    if (!eng) {
        fprintf(stderr, "Failed to initialise X12 engine\n");
        return 1;
    }

    x12_dump_status(eng);

    printf("\nOptimal thread count : %d\n", x12_get_optimal_threads(eng));
    printf("SIMD for 8KB data    : %s\n",  x12_should_use_simd(eng, 8192) ? "YES" : "NO");
    printf("Render backend       : %d\n",  x12_get_render_backend(eng));

    x12_shutdown(eng);
    return 0;
}
