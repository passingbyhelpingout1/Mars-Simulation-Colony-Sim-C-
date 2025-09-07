-#pragma once
-#include "time.hpp"
+#pragma once
+#include <chrono>
+#include "mars/timing.hpp"
 
 // Your game exposes these (or adapt names):
 struct Game; // holds world/sim state
 ...
 namespace eng {
-// Run a deterministic, accumulator-based fixed-timestep loop (Gaffer pattern).
+// Run a deterministic, accumulator-based fixed-timestep loop (Gaffer pattern).
 inline void run(Game& game) {
-  using namespace std::chrono;
+  using namespace std::chrono;
   auto t0 = clock::now();
   double accumulator = 0.0;
   ...
-  while (accumulator >= kFixedDt) {
-    sim_update(game, kFixedDt);
+  while (accumulator >= mars::kFixedDt) {
+    sim_update(game, mars::kFixedDt);
-    accumulator -= kFixedDt;
+    accumulator -= mars::kFixedDt;
   }
-  const double alpha = accumulator / kFixedDt;
+  const double alpha = accumulator / mars::kFixedDt;
   render_frame(game, alpha);
 }
 }
