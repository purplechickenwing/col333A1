#include "solver.h"
#include <iostream>
#include <chrono>

using namespace std;
// You can add any helper functions or classes you need here.

/**
 * @brief The main function to implement your search/optimization algorithm.
 * * This is a placeholder implementation. It creates a simple, likely invalid,
 * plan to demonstrate how to build the Solution object. 
 * * TODO: REPLACE THIS ENTIRE FUNCTION WITH YOUR ALGORITHM.
 */
Solution solve(const ProblemData& problem) {
    cout << "Starting solver..." << endl;

    Solution solution;

    // --- START OF PLACEHOLDER LOGIC ---
    // This is a naive example: send each helicopter on one trip to the first village.
    // This will definitely violate constraints but shows the structure.
    
    for (const auto& helicopter : problem.helicopters) {
        HelicopterPlan plan;
        plan.helicopter_id = helicopter.id;

        if (!problem.villages.empty()) {
            Trip trip;
            // Pickup 1 of each package type
            trip.dry_food_pickup = 1;
            trip.perishable_food_pickup = 1;
            trip.other_supplies_pickup = 1;

            // Drop them at the first village
            Drop drop;
            drop.village_id = problem.villages[0].id;
            drop.dry_food = 1;
            drop.perishable_food = 1;
            drop.other_supplies = 1;

            trip.drops.push_back(drop);
            plan.trips.push_back(trip);
        }
        solution.push_back(plan);
    }
    
    // --- END OF PLACEHOLDER LOGIC ---

    cout << "Solver finished." << endl;
    return solution;
}