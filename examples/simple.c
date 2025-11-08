/* Simple nblex example */

#include "nblex/nblex.h"
#include <stdio.h>

int main(void) {
  printf("nblex version: %s\n", nblex_version_string());

  nblex_world* world = nblex_world_new();
  if (!world) {
    fprintf(stderr, "Failed to create world\n");
    return 1;
  }

  if (nblex_world_open(world) != 0) {
    fprintf(stderr, "Failed to open world\n");
    nblex_world_free(world);
    return 1;
  }

  printf("World created and opened successfully\n");

  nblex_world_free(world);
  return 0;
}
