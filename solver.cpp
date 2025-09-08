#include "solver.h"
#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
#include <tuple>
#include <cmath>
#include <functional>


using namespace std;

// Structure to track village demands
struct VillageDemand {
    int meals_needed;
    int other_needed;
};

// Function to compute trip distance
double compute_trip_distance(const Trip& trip, int home_city_id, const ProblemData& problem, 
                            const std::vector<std::vector<double>>& distances) {
    if (trip.drops.empty()) return 0.0;
    if (home_city_id <= 0 || home_city_id > static_cast<int>(problem.cities.size()) ||
        trip.drops[0].village_id <= 0 || trip.drops[0].village_id > static_cast<int>(problem.villages.size())) {
        return 0.0;
    }
    double dist = distances[home_city_id - 1][problem.cities.size() + trip.drops[0].village_id - 1];
    for (size_t i = 1; i < trip.drops.size(); ++i) {
        if (trip.drops[i].village_id <= 0 || trip.drops[i].village_id > static_cast<int>(problem.villages.size()) ||
            trip.drops[i-1].village_id <= 0 || trip.drops[i-1].village_id > static_cast<int>(problem.villages.size())) {
            return 0.0;
        }
        dist += distances[problem.cities.size() + trip.drops[i-1].village_id - 1]
                        [problem.cities.size() + trip.drops[i].village_id - 1];
    }
    dist += distances[problem.cities.size() + trip.drops.back().village_id - 1][home_city_id - 1];
    if (std::isnan(dist) || dist < 0) {
        return 0.0;
    }
    return dist;
}

// Function to compute trip weight
double compute_trip_weight(const Trip& trip, const ProblemData& problem) {
    return trip.dry_food_pickup * problem.packages[0].weight +
           trip.perishable_food_pickup * problem.packages[1].weight +
           trip.other_supplies_pickup * problem.packages[2].weight;
}

// Function to compute objective value
double compute_objective(const Solution& sol, const ProblemData& problem, 
                        const std::vector<std::vector<double>>& distances) {
    double total_value = 0.0, total_cost = 0.0;
    std::vector<std::tuple<int, int, int>> delivered(problem.villages.size(), {0, 0, 0});

    for (const auto& plan : sol) {
        if (plan.helicopter_id < 1 || plan.helicopter_id > static_cast<int>(problem.helicopters.size())) {
            continue;
        }
        const auto& helicopter = problem.helicopters[plan.helicopter_id - 1];
        for (const auto& trip : plan.trips) {
            double trip_dist = compute_trip_distance(trip, helicopter.home_city_id, problem, distances);
            if (trip_dist <= 0) continue;
            total_cost += helicopter.fixed_cost + helicopter.alpha * trip_dist;
            for (const auto& drop : trip.drops) {
                if (drop.village_id < 1 || drop.village_id > static_cast<int>(problem.villages.size())) {
                    continue;
                }
                int idx = drop.village_id - 1;
                std::get<0>(delivered[idx]) += drop.dry_food;
                std::get<1>(delivered[idx]) += drop.perishable_food;
                std::get<2>(delivered[idx]) += drop.other_supplies;
            }
        }
    }

    for (size_t i = 0; i < problem.villages.size(); ++i) {
        int needed_meals = 9 * problem.villages[i].population;
        int delivered_dry = std::get<0>(delivered[i]);
        int delivered_peri = std::get<1>(delivered[i]);
        int total_food = delivered_dry + delivered_peri;
        double food_value = 0.0;
        int used_meals = std::min(total_food, needed_meals);
        // Prioritize perishable for value since v(p) > v(d)
        int peri_used = std::min(delivered_peri, used_meals);
        food_value += peri_used * problem.packages[1].value;
        int dry_used = used_meals - peri_used;
        food_value += dry_used * problem.packages[0].value;
        total_value += food_value;
        total_value += std::min(std::get<2>(delivered[i]), problem.villages[i].population) * 
                       problem.packages[2].value;
    }

    return total_value - total_cost;
}

// Function to allocate packages for a village
Drop allocate_packages(int village_id, double max_weight, VillageDemand& demand, 
                      const ProblemData& problem) {
    Drop drop;
    drop.village_id = village_id;
    drop.dry_food = 0;
    drop.perishable_food = 0;
    drop.other_supplies = 0;
    if (village_id < 1 || village_id > static_cast<int>(problem.villages.size())) {
        return drop;
    }

    int max_meals = 9 * problem.villages[village_id - 1].population;
    int max_other = problem.villages[village_id - 1].population;
    demand.meals_needed = std::min(demand.meals_needed, max_meals);
    demand.other_needed = std::min(demand.other_needed, max_other);

    double w_d = problem.packages[0].weight;
    double v_d = problem.packages[0].value;
    double w_p = problem.packages[1].weight;
    double v_p = problem.packages[1].value;
    double w_o = problem.packages[2].weight;
    double v_o = problem.packages[2].value;

    double max_value = 0.0;
    int best_d = 0, best_p = 0, best_o = 0;

    int max_o = std::min(demand.other_needed, static_cast<int>(max_weight / w_o));
    for (int n_o = 0; n_o <= max_o; ++n_o) {
        double rem_w = max_weight - n_o * w_o;
        int max_p = std::min(demand.meals_needed, static_cast<int>(rem_w / w_p));
        for (int n_p = 0; n_p <= max_p; ++n_p) {
            double rem_w2 = rem_w - n_p * w_p;
            int n_d = std::min(demand.meals_needed - n_p, static_cast<int>(rem_w2 / w_d));
            double value = n_d * v_d + n_p * v_p + n_o * v_o;
            if (value > max_value) {
                max_value = value;
                best_d = n_d;
                best_p = n_p;
                best_o = n_o;
            }
        }
    }

    drop.dry_food = best_d;
    drop.perishable_food = best_p;
    drop.other_supplies = best_o;
    demand.meals_needed -= best_d + best_p;
    demand.other_needed -= best_o;

    return drop;
}

// Function to create a greedy trip
Trip create_greedy_trip(const Helicopter& helicopter, std::vector<VillageDemand>& demand, 
                       const ProblemData& problem, const std::vector<std::vector<double>>& distances) {
    Trip trip;
    trip.dry_food_pickup = 0;
    trip.perishable_food_pickup = 0;
    trip.other_supplies_pickup = 0;
    trip.drops.clear();

    double total_weight = 0.0;
    double total_distance = 0.0;
    int home_city_id = helicopter.home_city_id;
    if (home_city_id < 1 || home_city_id > static_cast<int>(problem.cities.size())) {
        return trip;
    }

    // Sort villages by distance
    std::vector<std::pair<int, double>> village_distances;
    for (size_t i = 0; i < problem.villages.size(); ++i) {
        if (demand[i].meals_needed > 0 || demand[i].other_needed > 0) {
            int village_id = problem.villages[i].id;
            if (village_id < 1 || village_id > static_cast<int>(problem.villages.size())) continue;
            village_distances.emplace_back(
                village_id,
                distances[home_city_id - 1][problem.cities.size() + i]
            );
        }
    }
    std::sort(village_distances.begin(), village_distances.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    std::vector<Drop> drops;
    int last_loc = home_city_id - 1;
    for (size_t i = 0; i < std::min(village_distances.size(), size_t(2)); ++i) {
        const auto& [village_id, dist] = village_distances[i];
        if (village_id < 1 || village_id > static_cast<int>(problem.villages.size())) continue;
        double to_village_dist = distances[last_loc][problem.cities.size() + village_id - 1];
        double back_dist = distances[problem.cities.size() + village_id - 1][home_city_id - 1];
        if (total_distance + to_village_dist + back_dist > helicopter.distance_capacity) {
            continue;
        }

        auto temp_demand = demand[village_id - 1];
        Drop drop = allocate_packages(village_id, helicopter.weight_capacity - total_weight, 
                                     temp_demand, problem);
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
    if (total_distance > helicopter.distance_capacity || total_weight > helicopter.weight_capacity || total_distance <= 0) {
        return trip;
    }

    trip.drops = drops;
    trip.dry_food_pickup = 0;
    trip.perishable_food_pickup = 0;
    trip.other_supplies_pickup = 0;
    for (const auto& drop : drops) {
        trip.dry_food_pickup += drop.dry_food;
        trip.perishable_food_pickup += drop.perishable_food;
        trip.other_supplies_pickup += drop.other_supplies;
        if (drop.dry_food < 0 || drop.perishable_food < 0 || drop.other_supplies < 0) {
            return Trip{};
        }
    }

    return trip;
}

// Function to generate a random initial solution
Solution generate_random_solution(const ProblemData& problem, std::mt19937& rng, 
                                std::vector<VillageDemand>& village_demand, 
                                const std::vector<std::vector<double>>& distances) {
    Solution solution;
    std::vector<VillageDemand> temp_demand = village_demand;

    std::vector<int> heli_indices(problem.helicopters.size());
    for (size_t i = 0; i < heli_indices.size(); ++i) heli_indices[i] = i;
    std::shuffle(heli_indices.begin(), heli_indices.end(), rng);

    for (int idx : heli_indices) {
        if (idx < 0 || idx >= static_cast<int>(problem.helicopters.size())) continue;
        const auto& helicopter = problem.helicopters[idx];
        HelicopterPlan plan;
        plan.helicopter_id = helicopter.id;
        double remaining_distance = problem.d_max;

        std::uniform_int_distribution<> trip_dist(0, 5);
        int num_trips = trip_dist(rng);
        for (int t = 0; t < num_trips && remaining_distance > 0; ++t) {
            Trip trip;
            trip.dry_food_pickup = 0;
            trip.perishable_food_pickup = 0;
            trip.other_supplies_pickup = 0;
            trip.drops.clear();

            std::vector<int> village_ids;
            for (size_t i = 0; i < problem.villages.size(); ++i) {
                if (temp_demand[i].meals_needed > 0 || temp_demand[i].other_needed > 0) {
                    int village_id = problem.villages[i].id;
                    if (village_id < 1 || village_id > static_cast<int>(problem.villages.size())) continue;
                    village_ids.push_back(village_id);
                }
            }
            if (village_ids.empty()) continue;
            std::shuffle(village_ids.begin(), village_ids.end(), rng);
            int max_villages = std::min<size_t>(3, village_ids.size());
            std::uniform_int_distribution<> num_villages_dist(1, max_villages);
            int num_villages = num_villages_dist(rng);

            double total_weight = 0.0;
            double total_distance = 0.0;
            int last_loc = helicopter.home_city_id - 1;
            if (last_loc < 0 || last_loc >= static_cast<int>(problem.cities.size())) continue;

            for (int i = 0; i < num_villages; ++i) {
                int village_id = village_ids[i];
                if (village_id < 1 || village_id > static_cast<int>(problem.villages.size())) continue;
                double to_village_dist = distances[last_loc][problem.cities.size() + village_id - 1];
                double back_dist = distances[problem.cities.size() + village_id - 1][helicopter.home_city_id - 1];
                if (total_distance + to_village_dist + back_dist > helicopter.distance_capacity) {
                    continue;
                }

                auto temp_village_demand = temp_demand[village_id - 1];
                Drop drop = allocate_packages(village_id, helicopter.weight_capacity - total_weight, 
                                             temp_village_demand, problem);
                if (drop.dry_food == 0 && drop.perishable_food == 0 && drop.other_supplies == 0) {
                    continue;
                }

                drop.dry_food = int(0.5 * drop.dry_food + 0.5 * (rng() % drop.dry_food)); // Random fraction
                drop.perishable_food = int(0.5 * drop.perishable_food + 0.5 * (rng() % drop.perishable_food));
                drop.other_supplies = int(0.5 * drop.other_supplies + 0.5 * (rng() % drop.other_supplies));

                temp_village_demand.meals_needed = std::max(0, temp_village_demand.meals_needed - (drop.dry_food + drop.perishable_food));
                temp_village_demand.other_needed = std::max(0, temp_village_demand.other_needed - drop.other_supplies);

                trip.drops.push_back(drop);
                total_weight += drop.dry_food * problem.packages[0].weight +
                               drop.perishable_food * problem.packages[1].weight +
                               drop.other_supplies * problem.packages[2].weight;
                total_distance += to_village_dist;
                last_loc = problem.cities.size() + village_id - 1;
            }

            if (!trip.drops.empty()) {
                total_distance += distances[last_loc][helicopter.home_city_id - 1];
                if (total_distance <= helicopter.distance_capacity && 
                    total_weight <= helicopter.weight_capacity && total_distance > 0) {
                    trip.dry_food_pickup = 0;
                    trip.perishable_food_pickup = 0;
                    trip.other_supplies_pickup = 0;
                    for (const auto& drop : trip.drops) {
                        trip.dry_food_pickup += drop.dry_food;
                        trip.perishable_food_pickup += drop.perishable_food;
                        trip.other_supplies_pickup += drop.other_supplies;
                    }
                    if (total_distance <= remaining_distance) {
                        plan.trips.push_back(trip);
                        remaining_distance -= total_distance;
                    }
                }
            }
        }
        solution.push_back(plan);
    }

    village_demand = temp_demand;
    return solution;
}

// Function to generate a neighbor solution
Solution generate_neighbor(const Solution& sol, std::vector<VillageDemand>& demand, 
                         const ProblemData& problem, std::mt19937& rng,
                         const std::vector<std::vector<double>>& distances) {
    Solution neighbor = sol;
    if (problem.helicopters.empty()) return neighbor;
    std::uniform_int_distribution<> heli_dist(0, problem.helicopters.size() - 1);
    int h_idx = heli_dist(rng);
    if (h_idx < 0 || h_idx >= static_cast<int>(problem.helicopters.size())) {
        return neighbor;
    }

    double total_dist = 0.0;
    for (const auto& trip : neighbor[h_idx].trips) {
        total_dist += compute_trip_distance(trip, problem.helicopters[h_idx].home_city_id, problem, distances);
    }

    std::uniform_int_distribution<> action_dist(0, 3);
    int action = action_dist(rng);
    if (action == 0 && total_dist < problem.d_max) {
        auto temp_demand = demand;
        Trip trip = create_greedy_trip(problem.helicopters[h_idx], temp_demand, problem, distances);
        if (!trip.drops.empty()) {
            double trip_dist = compute_trip_distance(trip, problem.helicopters[h_idx].home_city_id, problem, distances);
            if (trip_dist > 0 && total_dist + trip_dist <= problem.d_max) {
                neighbor[h_idx].trips.push_back(trip);
                demand = temp_demand;
            }
        }
    } else if (action == 1 && !neighbor[h_idx].trips.empty()) {
        std::uniform_int_distribution<> trip_dist(0, neighbor[h_idx].trips.size() - 1);
        int t_idx = trip_dist(rng);
        auto& trip = neighbor[h_idx].trips[t_idx];
        for (const auto& drop : trip.drops) {
            if (drop.village_id < 1 || drop.village_id > static_cast<int>(problem.villages.size())) continue;
            demand[drop.village_id - 1].meals_needed = std::max(
                0, demand[drop.village_id - 1].meals_needed + drop.dry_food + drop.perishable_food
            );
            demand[drop.village_id - 1].other_needed = std::max(
                0, demand[drop.village_id - 1].other_needed + drop.other_supplies
            );
        }
        neighbor[h_idx].trips.erase(neighbor[h_idx].trips.begin() + t_idx);
    } else if (action == 2 && !neighbor[h_idx].trips.empty()) {
        std::uniform_int_distribution<> trip_dist(0, neighbor[h_idx].trips.size() - 1);
        int t_idx = trip_dist(rng);
        auto& trip = neighbor[h_idx].trips[t_idx];
        for (const auto& drop : trip.drops) {
            if (drop.village_id < 1 || drop.village_id > static_cast<int>(problem.villages.size())) continue;
            demand[drop.village_id - 1].meals_needed = std::max(
                0, demand[drop.village_id - 1].meals_needed + drop.dry_food + drop.perishable_food
            );
            demand[drop.village_id - 1].other_needed = std::max(
                0, demand[drop.village_id - 1].other_needed + drop.other_supplies
            );
        }
        trip = create_greedy_trip(problem.helicopters[h_idx], demand, problem, distances);
        if (!trip.drops.empty()) {
            for (const auto& drop : trip.drops) {
                if (drop.village_id < 1 || drop.village_id > static_cast<int>(problem.villages.size())) continue;
                demand[drop.village_id - 1].meals_needed = std::max(
                    0, demand[drop.village_id - 1].meals_needed - (drop.dry_food + drop.perishable_food)
                );
                demand[drop.village_id - 1].other_needed = std::max(
                    0, demand[drop.village_id - 1].other_needed - drop.other_supplies
                );
            }
        } else {
            neighbor[h_idx].trips.erase(neighbor[h_idx].trips.begin() + t_idx);
        }
    } else if (action == 3 && neighbor[h_idx].trips.size() >= 2) {
        std::uniform_int_distribution<> trip_dist(0, neighbor[h_idx].trips.size() - 1);
        int t1_idx = trip_dist(rng);
        int t2_idx = trip_dist(rng);
        while (t1_idx == t2_idx) t2_idx = trip_dist(rng);
        if (!neighbor[h_idx].trips[t1_idx].drops.empty() && !neighbor[h_idx].trips[t2_idx].drops.empty()) {
            std::uniform_int_distribution<> drop_dist1(0, neighbor[h_idx].trips[t1_idx].drops.size() - 1);
            std::uniform_int_distribution<> drop_dist2(0, neighbor[h_idx].trips[t2_idx].drops.size() - 1);
            int d1_idx = drop_dist1(rng);
            int d2_idx = drop_dist2(rng);
            std::swap(neighbor[h_idx].trips[t1_idx].drops[d1_idx], neighbor[h_idx].trips[t2_idx].drops[d2_idx]);
            // Update pickups
            for (auto& trip_ref : {std::ref(neighbor[h_idx].trips[t1_idx]), std::ref(neighbor[h_idx].trips[t2_idx])}) {
                Trip& t = trip_ref.get();
                t.dry_food_pickup = 0;
                t.perishable_food_pickup = 0;
                t.other_supplies_pickup = 0;
                for (const auto& drop : t.drops) {
                    t.dry_food_pickup += drop.dry_food;
                    t.perishable_food_pickup += drop.perishable_food;
                    t.other_supplies_pickup += drop.other_supplies;
                }
            }
        }
    }

    return neighbor;
}

Solution solve(const ProblemData& problem) {
    cout << "Starting solver..." << endl;

    // Input validation
    if (problem.time_limit_minutes < 0) {
        cerr << "Error: Negative time limit" << endl;
        return Solution();
    }
    if (problem.d_max < 0) {
        cerr << "Error: Negative d_max" << endl;
        return Solution();
    }
    if (problem.packages.size() != 3) {
        cerr << "Error: Exactly 3 package types required" << endl;
        return Solution();
    }
    for (const auto& pkg : problem.packages) {
        if (pkg.weight <= 0 || pkg.value < 0) {
            cerr << "Error: Invalid package weight or value" << endl;
            return Solution();
        }
    }
    if (problem.villages.empty()) {
        cerr << "Error: No villages provided" << endl;
        return Solution();
    }
    for (const auto& village : problem.villages) {
        if (village.id < 1 || village.id > static_cast<int>(problem.villages.size()) || 
            village.population < 0) {
            cerr << "Error: Invalid village ID or population" << endl;
            return Solution();
        }
    }
    if (problem.helicopters.empty()) {
        cerr << "Error: No helicopters provided" << endl;
        return Solution();
    }
    for (const auto& helicopter : problem.helicopters) {
        if (helicopter.id < 1 || helicopter.id > static_cast<int>(problem.helicopters.size()) ||
            helicopter.home_city_id < 1 || 
            helicopter.home_city_id > static_cast<int>(problem.cities.size()) ||
            helicopter.weight_capacity <= 0 || helicopter.distance_capacity <= 0 ||
            helicopter.fixed_cost < 0 || helicopter.alpha < 0) {
            cerr << "Error: Invalid helicopter parameters" << endl;
            return Solution();
        }
    }

    // Initialize random number generator
    std::random_device rd;
    std::mt19937 rng(rd());

    // Initialize village demands
    std::vector<VillageDemand> village_demand(problem.villages.size());
    for (size_t i = 0; i < problem.villages.size(); ++i) {
        village_demand[i].meals_needed = 9 * problem.villages[i].population;
        village_demand[i].other_needed = problem.villages[i].population;
    }

    // Compute distance matrix
    if (problem.cities.empty() || problem.villages.empty()) {
        cerr << "Error: Empty cities or villages" << endl;
        return Solution();
    }
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

    // Initialize best solution with greedy solution
    Solution best_solution;
    for (const auto& helicopter : problem.helicopters) {
        HelicopterPlan plan;
        plan.helicopter_id = helicopter.id;
        double remaining_distance = problem.d_max;

        while (remaining_distance > 0) {
            Trip trip = create_greedy_trip(helicopter, village_demand, problem, distances);
            if (trip.drops.empty()) break;
            double trip_dist = compute_trip_distance(trip, helicopter.home_city_id, problem, distances);
            if (trip_dist > remaining_distance || trip_dist <= 0) break;
            plan.trips.push_back(trip);
            remaining_distance -= trip_dist;
        }
        best_solution.push_back(plan);
    }
    double best_value = compute_objective(best_solution, problem, distances);
    cout << "Initial greedy value: " << best_value << endl;

    // Random restarts parameters
    const int max_restarts = 10;
    const int max_iterations_per_restart = 100;
    double time_limit_per_restart = problem.time_limit_minutes * 0.95 / max_restarts;

    // Hill climbing with random restarts
    auto start_time = std::chrono::steady_clock::now();
    for (int restart = 0; restart < max_restarts; ++restart) {
        auto current_time = std::chrono::steady_clock::now();
        double elapsed_minutes = std::chrono::duration<double>(current_time - start_time).count() / 60.0;
        if (elapsed_minutes >= problem.time_limit_minutes * 0.95) break;

        std::vector<VillageDemand> current_demand = village_demand;
        Solution current_solution = generate_random_solution(problem, rng, current_demand, distances);
        double current_value = compute_objective(current_solution, problem, distances);
        cout << "Restart " << restart + 1 << " initial value: " << current_value << endl;

        int iteration = 0;
        auto restart_start_time = std::chrono::steady_clock::now();
        while (iteration < max_iterations_per_restart) {
            auto iter_time = std::chrono::steady_clock::now();
            double iter_elapsed = std::chrono::duration<double>(iter_time - restart_start_time).count() / 60.0;
            if (iter_elapsed >= time_limit_per_restart || 
                elapsed_minutes + iter_elapsed >= problem.time_limit_minutes * 0.95) {
                break;
            }

            auto neighbor_demand = current_demand;
            Solution neighbor = generate_neighbor(current_solution, neighbor_demand, problem, rng, distances);
            double neighbor_value = compute_objective(neighbor, problem, distances);
            if (neighbor_value > current_value) {
                current_solution = neighbor;
                current_demand = neighbor_demand;
                current_value = neighbor_value;
            }

            if (current_value > best_value) {
                best_solution = current_solution;
                village_demand = current_demand;
                best_value = current_value;
                cout << "New best value at restart " << restart + 1 << ": " << best_value << endl;
            }
            ++iteration;
        }
    }

    cout << "Solver finished. Best value: " << best_value << endl;
    return best_solution;
}