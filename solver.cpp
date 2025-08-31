#include "solver.h"
#include <iostream>
#include <chrono>
#include <random>

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
    if (problem.time_limit_minutes < 0) {
        std::cerr << "Error: Negative time limit" << std::endl;
        return Solution();
    }
    if (problem.d_max < 0) {
        std::cerr << "Error: Negative d_max" << std::endl;
        return Solution();
    }
    if (problem.packages.size() != 3) {
        std::cerr << "Error: Exactly 3 package types (dry, perishable, other) required" << std::endl;
        return Solution();
    }
    for (const auto& pkg : problem.packages) {
        if (pkg.weight <= 0 || pkg.value < 0) { // Allow zero value but not zero weight
            std::cerr << "Error: Invalid package weight or value" << std::endl;
            return Solution();
        }
    }
    for (const auto& village : problem.villages) {
        if (village.id < 1 || village.id > static_cast<int>(problem.villages.size()) || village.population < 0) {
            std::cerr << "Error: Invalid village ID or negative population" << std::endl;
            return Solution();
        }
    }
    for (const auto& helicopter : problem.helicopters) {
        if (helicopter.id < 1 || helicopter.id > static_cast<int>(problem.helicopters.size()) ||
            helicopter.home_city_id < 1 || helicopter.home_city_id > static_cast<int>(problem.cities.size()) ||
            helicopter.weight_capacity <= 0 || helicopter.distance_capacity <= 0 ||
            helicopter.fixed_cost < 0 || helicopter.alpha < 0) {
            std::cerr << "Error: Invalid helicopter parameters" << std::endl;
            return Solution();
        }
    }

    // Initialize random number generator
    std::random_device rd;
    std::mt19937 rng(rd());

    // Structure to track village demands
    struct VillageDemand {
        int meals_needed;
        int other_needed;
    };
    std::vector<VillageDemand> village_demand(problem.villages.size());
    for (size_t i = 0; i < problem.villages.size(); ++i) {
        village_demand[i].meals_needed = 9 * problem.villages[i].population;
        village_demand[i].other_needed = problem.villages[i].population;
    }

    // Compute distance matrix
    std::vector<std::vector<double>> distances(
        problem.cities.size() + problem.villages.size(),
        std::vector<double>(problem.cities.size() + problem.villages.size(), 0.0)
    );
    for (size_t i = 0; i < problem.cities.size(); ++i) {
        for (size_t j = 0; j < problem.villages.size(); ++j) {
            distances[i][problem.cities.size() + j] = distances[problem.cities.size() + j][i] =
                distance(problem.cities[i], problem.villages[j].coords);
        }
    }
    for (size_t i = 0; i < problem.villages.size(); ++i) {
        for (size_t j = i + 1; j < problem.villages.size(); ++j) {
            distances[problem.cities.size() + i][problem.cities.size() + j] =
                distances[problem.cities.size() + j][problem.cities.size() + i] =
                distance(problem.villages[i].coords, problem.villages[j].coords);
        }
    }

    // Function to compute trip distance
    auto compute_trip_distance = [&](const Trip& trip, int home_city_id) {
        if (trip.drops.empty()) return 0.0;
        double dist = distances[home_city_id - 1][problem.cities.size() + trip.drops[0].village_id - 1];
        for (size_t i = 1; i < trip.drops.size(); ++i) {
            dist += distances[problem.cities.size() + trip.drops[i-1].village_id - 1]
                            [problem.cities.size() + trip.drops[i].village_id - 1];
        }
        dist += distances[problem.cities.size() + trip.drops.back().village_id - 1][home_city_id - 1];
        return dist;
    };

    // Function to compute trip weight
    auto compute_trip_weight = [&](const Trip& trip) {
        return trip.dry_food_pickup * problem.packages[0].weight +
               trip.perishable_food_pickup * problem.packages[1].weight +
               trip.other_supplies_pickup * problem.packages[2].weight;
    };

    double v_per_w_d = problem.packages[0].value / problem.packages[0].weight;
    double v_per_w_p = problem.packages[1].value / problem.packages[1].weight;
    bool prioritize_dry = v_per_w_d >= v_per_w_p;

    // Function to allocate packages for a village
    auto allocate_packages = [&](int village_id, double max_weight, VillageDemand& demand) {
        Drop drop;
        drop.village_id = village_id;
        double remaining_weight = max_weight;

        int first_food = 0;
        int second_food = 0;
        int first_index = prioritize_dry ? 0 : 1;
        int second_index = prioritize_dry ? 1 : 0;

        // Allocate first priority food
        first_food = std::min(
            std::max(demand.meals_needed, 0),
            static_cast<int>(remaining_weight / problem.packages[first_index].weight)
        );
        if (first_index == 0) drop.dry_food = first_food;
        else drop.perishable_food = first_food;
        remaining_weight -= first_food * problem.packages[first_index].weight;
        demand.meals_needed = std::max(0, demand.meals_needed - first_food);

        // Allocate second priority food for remaining meals
        second_food = std::min(
            std::max(demand.meals_needed, 0),
            static_cast<int>(remaining_weight / problem.packages[second_index].weight)
        );
        if (second_index == 0) drop.dry_food = second_food;
        else drop.perishable_food = second_food;
        remaining_weight -= second_food * problem.packages[second_index].weight;
        demand.meals_needed = std::max(0, demand.meals_needed - second_food);

        // Allocate other supplies
        drop.other_supplies = std::min(
            std::max(demand.other_needed, 0),
            static_cast<int>(remaining_weight / problem.packages[2].weight)
        );
        demand.other_needed = std::max(0, demand.other_needed - drop.other_supplies);

        return drop;
    };

    // Function to create a greedy trip
    auto create_greedy_trip = [&](const Helicopter& helicopter, std::vector<VillageDemand>& demand) {
        Trip trip;
        double total_weight = 0.0;
        double total_distance = 0.0;
        int home_city_id = helicopter.home_city_id;

        // Sort villages by distance from home city
        std::vector<std::pair<int, double>> village_distances;
        for (size_t i = 0; i < problem.villages.size(); ++i) {
            if (demand[i].meals_needed > 0 || demand[i].other_needed > 0) {
                village_distances.emplace_back(
                    problem.villages[i].id,
                    distances[home_city_id - 1][problem.cities.size() + i]
                );
            }
        }
        std::sort(village_distances.begin(), village_distances.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });

        // Limit to at most 2 villages per trip for efficiency
        std::vector<Drop> drops;
        int last_loc = home_city_id - 1;
        for (size_t i = 0; i < std::min(village_distances.size(), size_t(2)); ++i) {
            const auto& [village_id, dist] = village_distances[i];
            double to_village_dist = distances[last_loc][problem.cities.size() + village_id - 1];
            double back_dist = distances[problem.cities.size() + village_id - 1][home_city_id - 1];
            if (total_distance + to_village_dist + back_dist > helicopter.distance_capacity) {
                continue;
            }

            auto temp_demand = demand[village_id - 1];
            Drop drop = allocate_packages(village_id, helicopter.weight_capacity - total_weight, temp_demand);
            if (drop.dry_food == 0 && drop.perishable_food == 0 && drop.other_supplies == 0) {
                continue;
            }

            drops.push_back(drop);
            demand[village_id - 1] = temp_demand;
            total_weight += drop.dry_food * problem.packages[0].weight +
                           drop.perishable_food * problem.packages[1].weight +
                           drop.other_supplies * problem.packages[2].weight;
            total_distance += to_village_dist;
            last_loc = problem.cities.size() + village_id - 1;
        }

        if (drops.empty()) return trip;

        total_distance += distances[last_loc][home_city_id - 1];
        if (total_distance > helicopter.distance_capacity || total_weight > helicopter.weight_capacity) {
            return Trip{};
        }

        trip.drops = drops;
        trip.dry_food_pickup = 0;
        trip.perishable_food_pickup = 0;
        trip.other_supplies_pickup = 0;
        for (const auto& drop : drops) {
            trip.dry_food_pickup += drop.dry_food;
            trip.perishable_food_pickup += drop.perishable_food;
            trip.other_supplies_pickup += drop.other_supplies;
            // Validate non-negative quantities
            if (drop.dry_food < 0 || drop.perishable_food < 0 || drop.other_supplies < 0) {
                return Trip{}; // Invalidate trip if negative
            }
        }

        return trip;
    };

    // Function to compute objective value
    auto compute_objective = [&](const Solution& sol) {
        double total_value = 0.0, total_cost = 0.0;
        std::vector<std::pair<int, int>> delivered(problem.villages.size(), {0, 0}); // {meals, other}

        for (const auto& plan : sol) {
            const auto& helicopter = problem.helicopters[plan.helicopter_id - 1];
            for (const auto& trip : plan.trips) {
                total_cost += helicopter.fixed_cost + helicopter.alpha * compute_trip_distance(trip, helicopter.home_city_id);
                for (const auto& drop : trip.drops) {
                    int idx = drop.village_id - 1;
                    delivered[idx].first += drop.dry_food + drop.perishable_food;
                    delivered[idx].second += drop.other_supplies;
                }
            }
        }

        for (size_t i = 0; i < problem.villages.size(); ++i) {
            int meals = std::min(delivered[i].first, 9 * problem.villages[i].population);
            int meals_perishable = std::min(delivered[i].first, 9 * problem.villages[i].population);
            int meals_dry = meals - meals_perishable;
            if (meals_dry < 0) meals_dry = 0;
            total_value += meals_perishable * problem.packages[1].value + meals_dry * problem.packages[0].value;
            total_value += std::min(delivered[i].second, problem.villages[i].population) * problem.packages[2].value;
        }

        return total_value - total_cost;
    };

    // Function to generate a neighbor solution
    auto generate_neighbor = [&](Solution sol, std::vector<VillageDemand>& demand) {
        Solution neighbor = sol;
        std::uniform_int_distribution<> heli_dist(0, problem.helicopters.size() - 1);
        int h_idx = heli_dist(rng);

        // Compute current total distance for helicopter
        double total_dist = 0.0;
        for (const auto& trip : neighbor[h_idx].trips) {
            total_dist += compute_trip_distance(trip, problem.helicopters[h_idx].home_city_id);
        }

        // Randomly choose a neighbor type
        std::uniform_int_distribution<> action_dist(0, 2);
        int action = action_dist(rng);
        if (action == 0 && total_dist < problem.d_max) {
            // Add a new trip
            auto temp_demand = demand;
            Trip trip = create_greedy_trip(problem.helicopters[h_idx], temp_demand);
            if (!trip.drops.empty() && total_dist + compute_trip_distance(trip, problem.helicopters[h_idx].home_city_id) <= problem.d_max) {
                neighbor[h_idx].trips.push_back(trip);
                for (const auto& drop : trip.drops) {
                    demand[drop.village_id - 1].meals_needed = std::max(0, demand[drop.village_id - 1].meals_needed - (drop.dry_food + drop.perishable_food));
                    demand[drop.village_id - 1].other_needed = std::max(0, demand[drop.village_id - 1].other_needed - drop.other_supplies);
                }
            }
        } else if (action == 1 && !neighbor[h_idx].trips.empty()) {
            // Remove a random trip
            std::uniform_int_distribution<> trip_dist(0, neighbor[h_idx].trips.size() - 1);
            int t_idx = trip_dist(rng);
            auto& trip = neighbor[h_idx].trips[t_idx];
            // Restore demand before removing
            for (const auto& drop : trip.drops) {
                demand[drop.village_id - 1].meals_needed = std::max(0, demand[drop.village_id - 1].meals_needed + drop.dry_food + drop.perishable_food);
                demand[drop.village_id - 1].other_needed = std::max(0, demand[drop.village_id - 1].other_needed + drop.other_supplies);
            }
            neighbor[h_idx].trips.erase(neighbor[h_idx].trips.begin() + t_idx);
        } else if (action == 2 && !neighbor[h_idx].trips.empty()) {
            // Modify a trip
            std::uniform_int_distribution<> trip_dist(0, neighbor[h_idx].trips.size() - 1);
            int t_idx = trip_dist(rng);
            auto& trip = neighbor[h_idx].trips[t_idx];
            // Restore demand
            for (const auto& drop : trip.drops) {
                demand[drop.village_id - 1].meals_needed = std::max(0, demand[drop.village_id - 1].meals_needed + drop.dry_food + drop.perishable_food);
                demand[drop.village_id - 1].other_needed = std::max(0, demand[drop.village_id - 1].other_needed + drop.other_supplies);
            }
            trip = create_greedy_trip(problem.helicopters[h_idx], demand);
            if (!trip.drops.empty()) {
                for (const auto& drop : trip.drops) {
                    demand[drop.village_id - 1].meals_needed = std::max(0, demand[drop.village_id - 1].meals_needed - (drop.dry_food + drop.perishable_food));
                    demand[drop.village_id - 1].other_needed = std::max(0, demand[drop.village_id - 1].other_needed - drop.other_supplies);
                }
            } else {
                neighbor[h_idx].trips.erase(neighbor[h_idx].trips.begin() + t_idx);
            }
        }

        return neighbor;
    };

    for (const auto& helicopter : problem.helicopters) {
        HelicopterPlan plan;
        plan.helicopter_id = helicopter.id;
        double remaining_distance = problem.d_max;

        while (remaining_distance > 0) {
            Trip trip = create_greedy_trip(helicopter, village_demand);
            if (trip.drops.empty()) break;
            double trip_dist = compute_trip_distance(trip, helicopter.home_city_id);
            if (trip_dist > remaining_distance) break;
            plan.trips.push_back(trip);
            remaining_distance -= trip_dist;
            for (const auto& drop : trip.drops) {
                village_demand[drop.village_id - 1].meals_needed = std::max(0, village_demand[drop.village_id - 1].meals_needed - (drop.dry_food + drop.perishable_food));
                village_demand[drop.village_id - 1].other_needed = std::max(0, village_demand[drop.village_id - 1].other_needed - drop.other_supplies);
            }
        }
        solution.push_back(plan);
    }

    // Local search with iteration limit and time buffer
    auto start_time = std::chrono::steady_clock::now();
    double best_value = compute_objective(solution);
    const int max_iterations = 500; // Reduced for small input
    int iteration = 0;
    while (iteration < max_iterations) {
        auto current_time = std::chrono::steady_clock::now();
        double elapsed_minutes = std::chrono::duration<double>(current_time - start_time).count() / 60.0;
        if (elapsed_minutes >= problem.time_limit_minutes * 0.95) break;

        auto neighbor_demand = village_demand;
        Solution neighbor = generate_neighbor(solution, neighbor_demand);
        double neighbor_value = compute_objective(neighbor);
        if (neighbor_value > best_value) {
            solution = neighbor;
            village_demand = neighbor_demand;
            best_value = neighbor_value;
        }
        ++iteration;
    }
    // --- END OF PLACEHOLDER LOGIC ---

    cout << "Solver finished." << endl;
    return solution;
}