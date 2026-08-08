// Calc.h — V2.5.x Final PR (step 1 of 7)
//
// Owns the CG (centre-of-gravity) calculation. Extracted from
// CG_scale.ino's loop() as a header-only module — see docs/REFACTOR_PLAN.md.
//
// Header-only rationale: same as the other 10 modules in this project
// — see the pio-windows-esp8266 skill. PIO 6.x on Windows drops
// setup()/loop() when a second .cpp appears in src/.
//
// Cross-module dependencies (READ from other modules, must be defined
// in CG_scale.ino BEFORE this header is included):
//   - weightLoadCell[LC1..LC3]    from HX711Manager.h
//   - model.distance[X1..X3]      from Models.h (struct + global)
//   - model.virtualWeight[]       from Models.h
//   - model.mechanicsType         from Models.h
//   - nLoadcells                  from HX711Manager.h / CG_scale.ino
//   - LC1/LC2/LC3, X1/X2/X3,      from defaults.h
//     MAX_VIRTUAL_WEIGHT,
//     MINIMAL_TOTAL_WEIGHT,
//     MINIMAL_CG_WEIGHT
//
// What lives here:
//   - calcCG(): computes weightTotal, CG_length, CG_trans from the
//               current per-cell weights and model geometry. Called
//               once per OLED-update interval from loop().
//
// What stays in CG_scale.ino:
//   - weightTotal / CG_length / CG_trans as globals (cross-cutting
//     state, read by Display.h, WebApi.h, etc.)
//   - The (millis() - lastTimeMenu) > UPDATE_INTERVAL_OLED_MENU guard
//     — that's a loop() concern, not a calc() concern.
//   - printScaleOLED() — that's a Display.h concern, called after calcCG().

#pragma once

#include <Arduino.h>

// ---------- calcCG ----------
//
// Recomputes the global weightTotal, CG_length, CG_trans from the
// per-cell weights and the active model. Handles three cases:
//   - Below MINIMAL_TOTAL_WEIGHT: weight zeroed, CG zeroed (no
//     reliable measurement possible).
//   - MINIMAL_TOTAL_WEIGHT .. : CG computed for 1, 2 or 3 cells.
//   - mechanicsType 2/3: alternate longitudinal CG formula (model
//     uses different sign conventions for distance to the rear cell).
// Virtual weights: when enabled, the longitudinal CG is iteratively
// blended into the weighted average, and their weight is added to
// the total weight (note: order matters — CG blend FIRST, then
// add virtual weights, matching the original .ino sequence).
//
// Behavior is byte-identical to the original loop() block.

inline void calcCG() {
  // total model weight
  weightTotal = weightLoadCell[LC1] + weightLoadCell[LC2] + weightLoadCell[LC3];
  if (weightTotal < MINIMAL_TOTAL_WEIGHT && weightTotal > MINIMAL_TOTAL_WEIGHT * -1) {
    weightTotal = 0;
  }

  if (weightTotal > MINIMAL_CG_WEIGHT) {
    if (nLoadcells > 1) {
      // CG longitudinal axis
      CG_length = ((weightLoadCell[LC2] * model.distance[X2]) / weightTotal) + model.distance[X1];

      if (model.mechanicsType == 2) {
        CG_length = ((weightLoadCell[LC2] * model.distance[X2]) / weightTotal) - model.distance[X1];
      } else if (model.mechanicsType == 3) {
        CG_length = ((weightLoadCell[LC2] * model.distance[X2]) / weightTotal) * -1 + model.distance[X1];
      }

      for (int i = 0; i < MAX_VIRTUAL_WEIGHT; i++) {
        if (model.virtualWeight[i].enabled == true) {
          CG_length = (weightTotal * CG_length + model.virtualWeight[i].weight * model.virtualWeight[i].cg) / (weightTotal + model.virtualWeight[i].weight);
        }
      }

      for (int i = 0; i < MAX_VIRTUAL_WEIGHT; i++) {
        if (model.virtualWeight[i].enabled == true) {
          weightTotal += model.virtualWeight[i].weight;
        }
      }

      // CG transverse axis
      if (nLoadcells == 3) {
        CG_trans = (model.distance[X3] / 2) - (((weightLoadCell[LC1] + weightLoadCell[LC2] / 2) * model.distance[X3]) / weightTotal);
      }
    }
  } else {
    CG_length = 0;
    CG_trans = 0;
  }
}
