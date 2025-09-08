#include "solver.h"
#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
#include <tuple>
#include <cmath>
#include <functional>

using namespace std;

struct VillageNeeds {
    int food_packages_needed;
    int supplies_needed;
};

double calculate_trip_length(const Trip& trip, int base_city, const ProblemData& data, 
                             const vector<vector<double>>& dist_matrix) {
    if (trip.drops.empty()) return 0.0;
    if (base_city <= 0 || base_city > static_cast<int>(data.cities.size()) ||
        trip.drops[0].village_id <= 0 || trip.drops[0].village_id > static_cast<int>(data.villages.size())) {
        return 0.0;
    }
    double total_dist = dist_matrix[base_city - 1][data.cities.size() + trip.drops[0].village_id - 1];
    for (size_t idx = 1; idx < trip.drops.size(); ++idx) {
        if (trip.drops[idx].village_id <= 0 || trip.drops[idx].village_id > static_cast<int>(data.villages.size()) ||
            trip.drops[idx-1].village_id <= 0 || trip.drops[idx-1].village_id > static_cast<int>(data.villages.size())) {
            return 0.0;
        }
        total_dist += dist_matrix[data.cities.size() + trip.drops[idx-1].village_id - 1]
                          [data.cities.size() + trip.drops[idx].village_id - 1];
    }
    total_dist += dist_matrix[data.cities.size() + trip.drops.back().village_id - 1][base_city - 1];
    if (std::isnan(total_dist) || total_dist < 0) {
        return 0.0;
    }
    return total_dist;
}

double get_trip_weight(const Trip& trip, const ProblemData& data) {
    return trip.dry_food_pickup * data.packages[0].weight +
           trip.perishable_food_pickup * data.packages[1].weight +
           trip.other_supplies_pickup * data.packages[2].weight;
}

vector<tuple<int, int, int>> collect_deliveries(const Solution& solution, const ProblemData& data,
                                                const vector<vector<double>>& dist_matrix) {
    vector<tuple<int, int, int>> deliveries(data.villages.size(), {0, 0, 0});
    for (const auto& heli_plan : solution) {
        if (heli_plan.helicopter_id < 1 || heli_plan.helicopter_id > static_cast<int>(data.helicopters.size())) {
            continue;
        }
        const auto& heli = data.helicopters[heli_plan.helicopter_id - 1];
        double plan_dist = 0.0;
        for (const auto& trip : heli_plan.trips) {
            double trip_len = calculate_trip_length(trip, heli.home_city_id, data, dist_matrix);
            double trip_wt = get_trip_weight(trip, data);
            if (trip_len <= 0 || trip_wt > heli.weight_capacity + 1e-9 || trip_len > heli.distance_capacity + 1e-9) {
                continue;
            }
            plan_dist += trip_len;
            for (const auto& delivery : trip.drops) {
                if (delivery.village_id < 1 || delivery.village_id > static_cast<int>(data.villages.size())) {
                    continue;
                }
                int vil_idx = delivery.village_id - 1;
                get<0>(deliveries[vil_idx]) += delivery.dry_food;
                get<1>(deliveries[vil_idx]) += delivery.perishable_food;
                get<2>(deliveries[vil_idx]) += delivery.other_supplies;
            }
        }
        if (plan_dist > data.d_max + 1e-9) {
            continue;
        }
    }
    return deliveries;
}

double calculate_total_value(const vector<tuple<int, int, int>>& deliveries, const ProblemData& data) {
    double value_sum = 0.0;
    for (size_t vil = 0; vil < data.villages.size(); ++vil) {
        int req_food = 9 * data.villages[vil].population;
        int dry_del = get<0>(deliveries[vil]);
        int peri_del = get<1>(deliveries[vil]);
        int food_del = dry_del + peri_del;
        double food_val = 0.0;
        int food_used = min(food_del, req_food);
        int peri_used = min(peri_del, food_used);
        food_val += peri_used * data.packages[1].value;
        int dry_used = food_used - peri_used;
        food_val += dry_used * data.packages[0].value;
        value_sum += food_val;
        value_sum += min(get<2>(deliveries[vil]), data.villages[vil].population) * 
                     data.packages[2].value;
    }
    return value_sum;
}

double calculate_total_cost(const Solution& solution, const ProblemData& data,
                            const vector<vector<double>>& dist_matrix) {
    double cost_sum = 0.0;
    for (const auto& heli_plan : solution) {
        if (heli_plan.helicopter_id < 1 || heli_plan.helicopter_id > static_cast<int>(data.helicopters.size())) {
            continue;
        }
        const auto& heli = data.helicopters[heli_plan.helicopter_id - 1];
        double plan_dist = 0.0;
        bool used = false;
        for (const auto& trip : heli_plan.trips) {
            double trip_len = calculate_trip_length(trip, heli.home_city_id, data, dist_matrix);
            double trip_wt = get_trip_weight(trip, data);
            if (trip_len <= 0 || trip_wt > heli.weight_capacity + 1e-9 || trip_len > heli.distance_capacity + 1e-9) {
                continue;
            }
            plan_dist += trip_len;
            used = true;
        }
        if (plan_dist > data.d_max + 1e-9) {
            continue;
        }
        if (used) {
            cost_sum += heli.fixed_cost + heli.alpha * plan_dist;
        }
    }
    return cost_sum;
}

double evaluate_solution(const Solution& solution, const ProblemData& data, 
                         const vector<vector<double>>& dist_matrix) {
    auto deliveries = collect_deliveries(solution, data, dist_matrix);
    double total_val = calculate_total_value(deliveries, data);
    double total_cost = calculate_total_cost(solution, data, dist_matrix);
    return total_val - total_cost;
}

Drop determine_drop_mix(int vil_id, double wt_limit, VillageNeeds& needs, 
                        const ProblemData& data) {
    Drop delivery;
    delivery.village_id = vil_id;
    delivery.dry_food = 0;
    delivery.perishable_food = 0;
    delivery.other_supplies = 0;
    if (vil_id < 1 || vil_id > static_cast<int>(data.villages.size())) {
        return delivery;
    }

    int max_food = 9 * data.villages[vil_id - 1].population;
    int max_sup = data.villages[vil_id - 1].population;
    needs.food_packages_needed = min(needs.food_packages_needed, max_food);
    needs.supplies_needed = min(needs.supplies_needed, max_sup);

    double wt_dry = data.packages[0].weight;
    double val_dry = data.packages[0].value;
    double wt_peri = data.packages[1].weight;
    double val_peri = data.packages[1].value;
    double wt_sup = data.packages[2].weight;
    double val_sup = data.packages[2].value;

    double best_val = 0.0;
    int opt_dry = 0, opt_peri = 0, opt_sup = 0;

    int sup_max = min(needs.supplies_needed, static_cast<int>(wt_limit / wt_sup));
    for (int sup_count = 0; sup_count <= sup_max; ++sup_count) {
        double leftover_wt = wt_limit - sup_count * wt_sup;
        int peri_max = min(needs.food_packages_needed, static_cast<int>(leftover_wt / wt_peri));
        for (int peri_count = 0; peri_count <= peri_max; ++peri_count) {
            double leftover_wt2 = leftover_wt - peri_count * wt_peri;
            int dry_count = min(needs.food_packages_needed - peri_count, static_cast<int>(leftover_wt2 / wt_dry));
            double curr_val = dry_count * val_dry + peri_count * val_peri + sup_count * val_sup;
            if (curr_val > best_val) {
                best_val = curr_val;
                opt_dry = dry_count;
                opt_peri = peri_count;
                opt_sup = sup_count;
            }
        }
    }

    delivery.dry_food = opt_dry;
    delivery.perishable_food = opt_peri;
    delivery.other_supplies = opt_sup;
    needs.food_packages_needed -= opt_dry + opt_peri;
    needs.supplies_needed -= opt_sup;

    return delivery;
}

vector<pair<int, double>> sort_villages_by_distance(int base_city, const vector<VillageNeeds>& needs,
                                                    const ProblemData& data, 
                                                    const vector<vector<double>>& dist_matrix) {
    vector<pair<int, double>> vil_dists;
    for (size_t idx = 0; idx < data.villages.size(); ++idx) {
        if (needs[idx].food_packages_needed > 0 || needs[idx].supplies_needed > 0) {
            int vil_id = data.villages[idx].id;
            if (vil_id < 1 || vil_id > static_cast<int>(data.villages.size())) continue;
            vil_dists.emplace_back(
                vil_id,
                dist_matrix[base_city - 1][data.cities.size() + idx]
            );
        }
    }
    sort(vil_dists.begin(), vil_dists.end(),
         [](const auto& left, const auto& right) { return left.second < right.second; });
    return vil_dists;
}

Trip build_greedy_trip(const Helicopter& heli, vector<VillageNeeds>& needs, 
                       const ProblemData& data, const vector<vector<double>>& dist_matrix) {
    Trip trip_plan;
    trip_plan.dry_food_pickup = 0;
    trip_plan.perishable_food_pickup = 0;
    trip_plan.other_supplies_pickup = 0;
    trip_plan.drops.clear();

    double curr_weight = 0.0;
    double curr_dist = 0.0;
    int base_city = heli.home_city_id;
    if (base_city < 1 || base_city > static_cast<int>(data.cities.size())) {
        return trip_plan;
    }

    auto vil_dists = sort_villages_by_distance(base_city, needs, data, dist_matrix);

    vector<Drop> deliveries;
    int prev_loc = base_city - 1;
    for (size_t idx = 0; idx < min(vil_dists.size(), size_t(2)); ++idx) {
        const auto& [vil_id, dist] = vil_dists[idx];
        if (vil_id < 1 || vil_id > static_cast<int>(data.villages.size())) continue;
        double dist_to_vil = dist_matrix[prev_loc][data.cities.size() + vil_id - 1];
        double dist_back = dist_matrix[data.cities.size() + vil_id - 1][base_city - 1];
        if (curr_dist + dist_to_vil + dist_back > heli.distance_capacity) {
            continue;
        }

        auto temp_needs = needs[vil_id - 1];
        Drop delivery = determine_drop_mix(vil_id, heli.weight_capacity - curr_weight, 
                                           temp_needs, data);
        if (delivery.dry_food == 0 && delivery.perishable_food == 0 && delivery.other_supplies == 0) {
            continue;
        }

        deliveries.push_back(delivery);
        needs[vil_id - 1] = temp_needs;
        curr_weight += delivery.dry_food * data.packages[0].weight +
                       delivery.perishable_food * data.packages[1].weight +
                       delivery.other_supplies * data.packages[2].weight;
        curr_dist += dist_to_vil;
        prev_loc = data.cities.size() + vil_id - 1;
    }

    if (deliveries.empty()) return trip_plan;

    curr_dist += dist_matrix[prev_loc][base_city - 1];
    trip_plan.drops = deliveries;
    trip_plan.dry_food_pickup = 0;
    trip_plan.perishable_food_pickup = 0;
    trip_plan.other_supplies_pickup = 0;
    for (const auto& delivery : deliveries) {
        trip_plan.dry_food_pickup += delivery.dry_food;
        trip_plan.perishable_food_pickup += delivery.perishable_food;
        trip_plan.other_supplies_pickup += delivery.other_supplies;
        if (delivery.dry_food < 0 || delivery.perishable_food < 0 || delivery.other_supplies < 0) {
            return Trip{};
        }
    }
    double real_weight = get_trip_weight(trip_plan, data);
    if (curr_dist > heli.distance_capacity + 1e-9 || real_weight > heli.weight_capacity + 1e-9 || curr_dist <= 0) {
        return Trip{};
    }

    return trip_plan;
}

Solution create_random_solution(const ProblemData& data, mt19937& generator, 
                                vector<VillageNeeds>& vil_needs, 
                                const vector<vector<double>>& dist_matrix) {
    Solution sol;
    vector<VillageNeeds> curr_needs = vil_needs;

    vector<int> heli_order(data.helicopters.size());
    for (size_t i = 0; i < heli_order.size(); ++i) heli_order[i] = i;
    shuffle(heli_order.begin(), heli_order.end(), generator);

    for (int h_idx : heli_order) {
        if (h_idx < 0 || h_idx >= static_cast<int>(data.helicopters.size())) continue;
        const auto& heli = data.helicopters[h_idx];
        HelicopterPlan plan;
        plan.helicopter_id = heli.id;
        double leftover_dist = data.d_max;

        uniform_int_distribution<> trips_gen(0, 5);
        int trip_count = trips_gen(generator);
        for (int trip_num = 0; trip_num < trip_count && leftover_dist > 0; ++trip_num) {
            Trip curr_trip;
            curr_trip.dry_food_pickup = 0;
            curr_trip.perishable_food_pickup = 0;
            curr_trip.other_supplies_pickup = 0;
            curr_trip.drops.clear();

            vector<int> possible_vils;
            int base_idx = heli.home_city_id - 1;
            for (size_t v = 0; v < data.villages.size(); ++v) {
                if (curr_needs[v].food_packages_needed > 0 || curr_needs[v].supplies_needed > 0) {
                    int vil_id = data.villages[v].id;
                    if (vil_id < 1 || vil_id > static_cast<int>(data.villages.size())) continue;
                    double rt_dist = 2 * dist_matrix[base_idx][data.cities.size() + v];
                    if (rt_dist <= heli.distance_capacity) {
                        possible_vils.push_back(vil_id);
                    }
                }
            }
            if (possible_vils.empty()) continue;
            shuffle(possible_vils.begin(), possible_vils.end(), generator);
            int vil_limit = min(static_cast<int>(possible_vils.size()), 3);
            uniform_int_distribution<> vil_count_gen(1, vil_limit);
            int vil_count = vil_count_gen(generator);

            double total_wt = 0.0;
            double total_len = 0.0;
            int prev_loc = heli.home_city_id - 1;
            if (prev_loc < 0 || prev_loc >= static_cast<int>(data.cities.size())) continue;

            for (int v_num = 0; v_num < vil_count; ++v_num) {
                int vil_id = possible_vils[v_num];
                if (vil_id < 1 || vil_id > static_cast<int>(data.villages.size())) continue;
                double len_to_vil = dist_matrix[prev_loc][data.cities.size() + vil_id - 1];
                double len_back = dist_matrix[data.cities.size() + vil_id - 1][heli.home_city_id - 1];
                if (total_len + len_to_vil + len_back > heli.distance_capacity) {
                    continue;
                }

                auto vil_needs_temp = curr_needs[vil_id - 1];
                Drop delivery = determine_drop_mix(vil_id, heli.weight_capacity - total_wt, 
                                                   vil_needs_temp, data);
                if (delivery.dry_food == 0 && delivery.perishable_food == 0 && delivery.other_supplies == 0) {
                    continue;
                }

                int full_d = delivery.dry_food;
                int full_p = delivery.perishable_food;
                int full_s = delivery.other_supplies;

                if (full_d > 0) {
                    uniform_int_distribution<int> d_gen(1, full_d);
                    delivery.dry_food = d_gen(generator);
                }
                if (full_p > 0) {
                    uniform_int_distribution<int> p_gen(1, full_p);
                    delivery.perishable_food = p_gen(generator);
                }
                if (full_s > 0) {
                    uniform_int_distribution<int> s_gen(1, full_s);
                    delivery.other_supplies = s_gen(generator);
                }

                if (delivery.dry_food == 0 && delivery.perishable_food == 0 && delivery.other_supplies == 0) {
                    vil_needs_temp.food_packages_needed += full_d + full_p;
                    vil_needs_temp.supplies_needed += full_s;
                    continue;
                }

                int used_food = delivery.dry_food + delivery.perishable_food;
                int used_sup = delivery.other_supplies;
                vil_needs_temp.food_packages_needed += (full_d + full_p - used_food);
                vil_needs_temp.supplies_needed += (full_s - used_sup);
                vil_needs_temp.food_packages_needed = max(0, vil_needs_temp.food_packages_needed);
                vil_needs_temp.supplies_needed = max(0, vil_needs_temp.supplies_needed);

                curr_needs[vil_id - 1] = vil_needs_temp;

                curr_trip.drops.push_back(delivery);
                total_wt += delivery.dry_food * data.packages[0].weight +
                            delivery.perishable_food * data.packages[1].weight +
                            delivery.other_supplies * data.packages[2].weight;
                total_len += len_to_vil;
                prev_loc = data.cities.size() + vil_id - 1;
            }

            if (!curr_trip.drops.empty()) {
                total_len += dist_matrix[prev_loc][heli.home_city_id - 1];
                curr_trip.dry_food_pickup = 0;
                curr_trip.perishable_food_pickup = 0;
                curr_trip.other_supplies_pickup = 0;
                for (const auto& delivery : curr_trip.drops) {
                    curr_trip.dry_food_pickup += delivery.dry_food;
                    curr_trip.perishable_food_pickup += delivery.perishable_food;
                    curr_trip.other_supplies_pickup += delivery.other_supplies;
                }
                double real_wt = get_trip_weight(curr_trip, data);
                if (total_len <= heli.distance_capacity + 1e-9 && 
                    real_wt <= heli.weight_capacity + 1e-9 && total_len > 0) {
                    if (total_len <= leftover_dist) {
                        plan.trips.push_back(curr_trip);
                        leftover_dist -= total_len;
                    }
                }
            }
        }
        sol.push_back(plan);
    }

    vil_needs = curr_needs;
    return sol;
}

void add_trip_to_plan(HelicopterPlan& plan, const Helicopter& heli, vector<VillageNeeds>& needs,
                      const ProblemData& data, const vector<vector<double>>& dist_matrix, double& plan_dist) {
    auto temp_needs = needs;
    Trip added_trip = build_greedy_trip(heli, temp_needs, data, dist_matrix);
    if (!added_trip.drops.empty()) {
        double added_len = calculate_trip_length(added_trip, heli.home_city_id, data, dist_matrix);
        if (added_len > 0 && plan_dist + added_len <= data.d_max + 1e-9) {
            plan.trips.push_back(added_trip);
            needs = temp_needs;
            plan_dist += added_len;
        }
    }
}

void remove_trip_from_plan(HelicopterPlan& plan, vector<VillageNeeds>& needs, int trip_idx,
                           const Helicopter& heli, const ProblemData& data, 
                           const vector<vector<double>>& dist_matrix, double& plan_dist) {
    const auto& removed_trip = plan.trips[trip_idx];
    for (const auto& delivery : removed_trip.drops) {
        if (delivery.village_id < 1 || delivery.village_id > static_cast<int>(data.villages.size())) continue;
        needs[delivery.village_id - 1].food_packages_needed = max(
            0, needs[delivery.village_id - 1].food_packages_needed + delivery.dry_food + delivery.perishable_food
        );
        needs[delivery.village_id - 1].supplies_needed = max(
            0, needs[delivery.village_id - 1].supplies_needed + delivery.other_supplies
        );
    }
    double removed_len = calculate_trip_length(removed_trip, heli.home_city_id, data, dist_matrix);
    plan.trips.erase(plan.trips.begin() + trip_idx);
    plan_dist -= removed_len;
}

void replace_trip_in_plan(HelicopterPlan& plan, int trip_idx, const Helicopter& heli, 
                          vector<VillageNeeds>& needs, const ProblemData& data, 
                          const vector<vector<double>>& dist_matrix, double& plan_dist) {
    Trip& old_trip = plan.trips[trip_idx];
    double old_len = calculate_trip_length(old_trip, heli.home_city_id, data, dist_matrix);
    auto temp_needs = needs;
    for (const auto& delivery : old_trip.drops) {
        if (delivery.village_id < 1 || delivery.village_id > static_cast<int>(data.villages.size())) continue;
        int vil_idx = delivery.village_id - 1;
        temp_needs[vil_idx].food_packages_needed += delivery.dry_food + delivery.perishable_food;
        temp_needs[vil_idx].supplies_needed += delivery.other_supplies;
    }
    Trip replacement = build_greedy_trip(heli, temp_needs, data, dist_matrix);
    double new_len = calculate_trip_length(replacement, heli.home_city_id, data, dist_matrix);
    double new_plan_dist = plan_dist - old_len + new_len;
    if (!replacement.drops.empty() && new_len > 0 && new_plan_dist <= data.d_max + 1e-9) {
        double new_wt = get_trip_weight(replacement, data);
        if (new_wt <= heli.weight_capacity + 1e-9 && new_len <= heli.distance_capacity + 1e-9) {
            needs = temp_needs;
            old_trip = replacement;
            plan_dist = new_plan_dist;
        }
    }
}

void swap_drops_between_trips(HelicopterPlan& plan, int t1_idx, int t2_idx, int d1_idx, int d2_idx,
                              const Helicopter& heli, const ProblemData& data, 
                              const vector<vector<double>>& dist_matrix, double& plan_dist) {
    auto& trip1 = plan.trips[t1_idx];
    auto& trip2 = plan.trips[t2_idx];
    double len1_old = calculate_trip_length(trip1, heli.home_city_id, data, dist_matrix);
    double len2_old = calculate_trip_length(trip2, heli.home_city_id, data, dist_matrix);

    swap(trip1.drops[d1_idx], trip2.drops[d2_idx]);

    trip1.dry_food_pickup = 0;
    trip1.perishable_food_pickup = 0;
    trip1.other_supplies_pickup = 0;
    for (const auto& del : trip1.drops) {
        trip1.dry_food_pickup += del.dry_food;
        trip1.perishable_food_pickup += del.perishable_food;
        trip1.other_supplies_pickup += del.other_supplies;
    }
    trip2.dry_food_pickup = 0;
    trip2.perishable_food_pickup = 0;
    trip2.other_supplies_pickup = 0;
    for (const auto& del : trip2.drops) {
        trip2.dry_food_pickup += del.dry_food;
        trip2.perishable_food_pickup += del.perishable_food;
        trip2.other_supplies_pickup += del.other_supplies;
    }

    double len1_new = calculate_trip_length(trip1, heli.home_city_id, data, dist_matrix);
    double len2_new = calculate_trip_length(trip2, heli.home_city_id, data, dist_matrix);
    double wt1_new = get_trip_weight(trip1, data);
    double wt2_new = get_trip_weight(trip2, data);
    double new_plan_dist = plan_dist - len1_old - len2_old + len1_new + len2_new;
    bool ok = (wt1_new <= heli.weight_capacity + 1e-9) &&
              (wt2_new <= heli.weight_capacity + 1e-9) &&
              (len1_new <= heli.distance_capacity + 1e-9) &&
              (len2_new <= heli.distance_capacity + 1e-9) &&
              (new_plan_dist <= data.d_max + 1e-9);
    if (!ok) {
        swap(trip1.drops[d1_idx], trip2.drops[d2_idx]);
        trip1.dry_food_pickup = 0;
        trip1.perishable_food_pickup = 0;
        trip1.other_supplies_pickup = 0;
        for (const auto& del : trip1.drops) {
            trip1.dry_food_pickup += del.dry_food;
            trip1.perishable_food_pickup += del.perishable_food;
            trip1.other_supplies_pickup += del.other_supplies;
        }
        trip2.dry_food_pickup = 0;
        trip2.perishable_food_pickup = 0;
        trip2.other_supplies_pickup = 0;
        for (const auto& del : trip2.drops) {
            trip2.dry_food_pickup += del.dry_food;
            trip2.perishable_food_pickup += del.perishable_food;
            trip2.other_supplies_pickup += del.other_supplies;
        }
    } else {
        plan_dist = new_plan_dist;
    }
}

Solution create_neighbor(const Solution& current_sol, vector<VillageNeeds>& needs, 
                         const ProblemData& data, mt19937& generator,
                         const vector<vector<double>>& dist_matrix) {
    Solution neighbor_sol = current_sol;
    if (data.helicopters.empty()) return neighbor_sol;
    uniform_int_distribution<> heli_gen(0, data.helicopters.size() - 1);
    int heli_idx = heli_gen(generator);
    if (heli_idx < 0 || heli_idx >= static_cast<int>(data.helicopters.size())) {
        return neighbor_sol;
    }
    const auto& heli = data.helicopters[heli_idx];
    auto& plan = neighbor_sol[heli_idx];

    double curr_plan_dist = 0.0;
    for (const auto& trip : plan.trips) {
        curr_plan_dist += calculate_trip_length(trip, heli.home_city_id, data, dist_matrix);
    }

    uniform_int_distribution<> op_gen(0, 3);
    int operation = op_gen(generator);
    if (operation == 0 && curr_plan_dist < data.d_max) {
        add_trip_to_plan(plan, heli, needs, data, dist_matrix, curr_plan_dist);
    } else if (operation == 1 && !plan.trips.empty()) {
        uniform_int_distribution<> trip_gen(0, plan.trips.size() - 1);
        int t_idx = trip_gen(generator);
        remove_trip_from_plan(plan, needs, t_idx, heli, data, dist_matrix, curr_plan_dist);
    } else if (operation == 2 && !plan.trips.empty()) {
        uniform_int_distribution<> trip_gen(0, plan.trips.size() - 1);
        int t_idx = trip_gen(generator);
        replace_trip_in_plan(plan, t_idx, heli, needs, data, dist_matrix, curr_plan_dist);
    } else if (operation == 3 && plan.trips.size() >= 2) {
        uniform_int_distribution<> trip_gen(0, plan.trips.size() - 1);
        int t1 = trip_gen(generator);
        int t2 = trip_gen(generator);
        while (t1 == t2) t2 = trip_gen(generator);
        if (!plan.trips[t1].drops.empty() && !plan.trips[t2].drops.empty()) {
            uniform_int_distribution<> drop_gen1(0, plan.trips[t1].drops.size() - 1);
            uniform_int_distribution<> drop_gen2(0, plan.trips[t2].drops.size() - 1);
            int d1 = drop_gen1(generator);
            int d2 = drop_gen2(generator);
            swap_drops_between_trips(plan, t1, t2, d1, d2, heli, data, dist_matrix, curr_plan_dist);
        }
    }

    return neighbor_sol;
}

bool validate_input(const ProblemData& data) {
    if (data.time_limit_minutes < 0) {
        cerr << "Error: Negative time limit" << endl;
        return false;
    }
    if (data.d_max < 0) {
        cerr << "Error: Negative d_max" << endl;
        return false;
    }
    if (data.packages.size() != 3) {
        cerr << "Error: Exactly 3 package types required" << endl;
        return false;
    }
    for (const auto& pkg : data.packages) {
        if (pkg.weight <= 0 || pkg.value < 0) {
            cerr << "Error: Invalid package weight or value" << endl;
            return false;
        }
    }
    if (data.villages.empty()) {
        cerr << "Error: No villages provided" << endl;
        return false;
    }
    for (const auto& vil : data.villages) {
        if (vil.id < 1 || vil.id > static_cast<int>(data.villages.size()) || 
            vil.population < 0) {
            cerr << "Error: Invalid village ID or population" << endl;
            return false;
        }
    }
    if (data.helicopters.empty()) {
        cerr << "Error: No helicopters provided" << endl;
        return false;
    }
    for (const auto& heli : data.helicopters) {
        if (heli.id < 1 || heli.id > static_cast<int>(data.helicopters.size()) ||
            heli.home_city_id < 1 || 
            heli.home_city_id > static_cast<int>(data.cities.size()) ||
            heli.weight_capacity <= 0 || heli.distance_capacity <= 0 ||
            heli.fixed_cost < 0 || heli.alpha < 0) {
            cerr << "Error: Invalid helicopter parameters" << endl;
            return false;
        }
    }
    if (data.cities.empty()) {
        cerr << "Error: Empty cities" << endl;
        return false;
    }
    return true;
}

vector<vector<double>> build_distance_matrix(const ProblemData& data) {
    size_t total_locs = data.cities.size() + data.villages.size();
    vector<vector<double>> dists(total_locs, vector<double>(total_locs, 0.0));
    for (size_t c = 0; c < data.cities.size(); ++c) {
        for (size_t v = 0; v < data.villages.size(); ++v) {
            double d = distance(data.cities[c], data.villages[v].coords);
            dists[c][data.cities.size() + v] = d;
            dists[data.cities.size() + v][c] = d;
        }
    }
    for (size_t v1 = 0; v1 < data.villages.size(); ++v1) {
        for (size_t v2 = v1 + 1; v2 < data.villages.size(); ++v2) {
            double d = distance(data.villages[v1].coords, data.villages[v2].coords);
            dists[data.cities.size() + v1][data.cities.size() + v2] = d;
            dists[data.cities.size() + v2][data.cities.size() + v1] = d;
        }
    }
    return dists;
}

vector<VillageNeeds> init_village_needs(const ProblemData& data) {
    vector<VillageNeeds> needs(data.villages.size());
    for (size_t i = 0; i < data.villages.size(); ++i) {
        needs[i].food_packages_needed = 9 * data.villages[i].population;
        needs[i].supplies_needed = data.villages[i].population;
    }
    return needs;
}

Solution create_greedy_solution(const ProblemData& data, vector<VillageNeeds>& needs, 
                                const vector<vector<double>>& dist_matrix) {
    Solution sol;
    auto curr_needs = needs;
    for (const auto& heli : data.helicopters) {
        HelicopterPlan plan;
        plan.helicopter_id = heli.id;
        double leftover_dist = data.d_max;

        while (leftover_dist > 0) {
            Trip trip = build_greedy_trip(heli, curr_needs, data, dist_matrix);
            if (trip.drops.empty()) break;
            double trip_len = calculate_trip_length(trip, heli.home_city_id, data, dist_matrix);
            if (trip_len > leftover_dist + 1e-9 || trip_len <= 0) break;
            plan.trips.push_back(trip);
            leftover_dist -= trip_len;
        }
        sol.push_back(plan);
    }
    needs = curr_needs;
    return sol;
}

void cleanup_solution(Solution& sol, const ProblemData& data, const vector<vector<double>>& dist_matrix) {
    for (auto& plan : sol) {
        if (plan.helicopter_id < 1 || plan.helicopter_id > static_cast<int>(data.helicopters.size())) {
            plan.trips.clear();
            continue;
        }
        const auto& heli = data.helicopters[plan.helicopter_id - 1];
        vector<Trip> ok_trips;
        double total_heli_dist = 0.0;
        for (const auto& trip : plan.trips) {
            double t_len = calculate_trip_length(trip, heli.home_city_id, data, dist_matrix);
            double t_wt = get_trip_weight(trip, data);
            if (t_len > 0 && t_len <= heli.distance_capacity + 1e-9 && t_wt <= heli.weight_capacity + 1e-9) {
                ok_trips.push_back(trip);
                total_heli_dist += t_len;
            }
        }
        while (total_heli_dist > data.d_max + 1e-9 && !ok_trips.empty()) {
            size_t longest_idx = 0;
            double max_len = 0.0;
            for (size_t t = 0; t < ok_trips.size(); ++t) {
                double td = calculate_trip_length(ok_trips[t], heli.home_city_id, data, dist_matrix);
                if (td > max_len) {
                    max_len = td;
                    longest_idx = t;
                }
            }
            ok_trips.erase(ok_trips.begin() + longest_idx);
            total_heli_dist -= max_len;
        }
        plan.trips = ok_trips;
    }
}

Solution solve(const ProblemData& problem) {
    cout << "Starting solver..." << endl;

    if (!validate_input(problem)) {
        return Solution();
    }

    random_device rand_dev;
    mt19937 generator(rand_dev());

    auto vil_needs = init_village_needs(problem);
    auto orig_needs = vil_needs;

    auto dist_matrix = build_distance_matrix(problem);

    auto greedy_sol = create_greedy_solution(problem, vil_needs, dist_matrix);
    double best_obj = evaluate_solution(greedy_sol, problem, dist_matrix);
    Solution optimal_sol = greedy_sol;
    cout << "Initial greedy value: " << best_obj << endl;

    const int restart_limit = 10;
    const int iter_limit = 100;
    double restart_time_budget = problem.time_limit_minutes * 0.95 / restart_limit;

    auto global_start = chrono::steady_clock::now();
    for (int restart_num = 0; restart_num < restart_limit; ++restart_num) {
        auto now = chrono::steady_clock::now();
        double mins_elapsed = chrono::duration<double>(now - global_start).count() / 60.0;
        if (mins_elapsed >= problem.time_limit_minutes * 0.95) break;

        vector<VillageNeeds> restart_needs = orig_needs;
        Solution curr_sol = create_random_solution(problem, generator, restart_needs, dist_matrix);
        double curr_obj = evaluate_solution(curr_sol, problem, dist_matrix);
        cout << "Restart " << restart_num + 1 << " initial value: " << curr_obj << endl;

        int iter_count = 0;
        auto restart_begin = chrono::steady_clock::now();
        while (iter_count < iter_limit) {
            auto iter_now = chrono::steady_clock::now();
            double iter_mins = chrono::duration<double>(iter_now - restart_begin).count() / 60.0;
            if (iter_mins >= restart_time_budget || 
                mins_elapsed + iter_mins >= problem.time_limit_minutes * 0.95) {
                break;
            }

            auto neigh_needs = restart_needs;
            Solution neigh_sol = create_neighbor(curr_sol, neigh_needs, problem, generator, dist_matrix);
            double neigh_obj = evaluate_solution(neigh_sol, problem, dist_matrix);
            if (neigh_obj > curr_obj) {
                curr_sol = neigh_sol;
                restart_needs = neigh_needs;
                curr_obj = neigh_obj;
            }

            if (curr_obj > best_obj) {
                optimal_sol = curr_sol;
                best_obj = curr_obj;
                cout << "New best value at restart " << restart_num + 1 << ": " << best_obj << endl;
            }
            ++iter_count;
        }
    }

    cleanup_solution(optimal_sol, problem, dist_matrix);
    best_obj = evaluate_solution(optimal_sol, problem, dist_matrix);
    cout << "Solver finished. Best value: " << best_obj << endl;
    return optimal_sol;
}
