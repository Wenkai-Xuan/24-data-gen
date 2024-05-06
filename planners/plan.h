#pragma once

#include "spdlog/spdlog.h"

#include <string>
#include <vector>

#include "json/json.h"

#include <Core/array.h>

#include "common/util.h"
#include "common/env_util.h"
#include "common/types.h"
#include "common/config.h"

using json = nlohmann::json;

typedef std::vector<arr> TaskPoses;
typedef std::unordered_map<RobotTaskPair, std::vector<TaskPoses>> RobotTaskPoseMap;

typedef std::vector<RobotTaskPair> OrderedTaskSequence;
typedef std::unordered_map<Robot, std::vector<Task>> UnorderedTaskSequence;

OrderedTaskSequence load_sequence_from_json(const std::string &path, std::vector<Robot> robots) {
  std::ifstream ifs(path);
  json jf = json::parse(ifs);

  OrderedTaskSequence seq;
  for (const auto &item : jf["tasks"].items()) {
    const std::string primitive = item.value()["primitive"];
    const int object = item.value()["object"];
    const std::vector<std::string> robot_prefixes = item.value()["robots"];

    RobotTaskPair rtp;
    for (const auto &prefix: robot_prefixes){
      // search for the robot with the correct prefix
      for (const auto &robot: robots){
        if (robot.prefix == prefix){
          rtp.robots.push_back(robot);
          break;
        }
      }
    }

    rtp.task.object = object;
    rtp.task.type = string_to_primitive_type(primitive);

    seq.push_back(rtp);
  }

  return seq;
}

json ordered_sequence_to_json(OrderedTaskSequence seq) {
  // pass
  json data;
  json tasks;

  for (const auto &s : seq) {
    json task;
    task["primitive"] = primitive_type_to_string(s.task.type);
    task["object"] = s.task.object;

    std::vector<std::string> prefixes;
    for (const auto &r: s.robots){
      prefixes.push_back(r.prefix);
    }

    task["robots"] = prefixes;

    tasks.push_back(task);
  }
  
  data["tasks"] = tasks;

  return data;
}

std::string ordered_sequence_to_str(OrderedTaskSequence seq) {
  std::stringstream ss;
  for (const auto &s : seq) {
    ss << "(" << s.serialize() << ")";
  }
  return ss.str();
}

template <> struct std::hash<OrderedTaskSequence> {
  std::size_t operator()(OrderedTaskSequence const &seq) const {
    std::hash<std::string> hasher;
    return hasher(ordered_sequence_to_str(seq));
  }
};

struct ComputeStatistics {
  double total_compute_time;

  double rrt_compute_time;
  double rrt_plan_time;
  double rrt_nn_time;
  double rrt_coll_time;
  double rrt_shortcut_time;
  double rrt_smoothing_time;

  double komo_compute_time;
};

// this is the solution of one task
// TODO: add constraints that this solution needs to fulfill
struct TaskPart {
  bool has_solution = false;

  TaskPart(const arr &_t, const arr &_path)
      : has_solution(true), t(_t), path(_path){};
  TaskPart(){};

  rai::Animation::AnimationPart anim;

  arr t;
  arr path;

  Robot r;
  uint task_index;

  PrimitiveType primitive_type;
  // action name
  std::string name;

  std::string algorithm;

  bool is_exit = false;

  ComputeStatistics stats;
};

typedef std::unordered_map<Robot, std::vector<TaskPart>> Plan;

json get_plan_as_json(const Plan& plan) {
  json data;

  for (const auto &per_robot_plan : plan) {
    const auto robot = per_robot_plan.first;
    const auto tasks = per_robot_plan.second;

    json tmp;
    tmp["robot"] = robot.prefix;

    for (const auto &task : tasks) {
      json task_description;
      task_description["name"] = task.name;
      task_description["algorithm"] = task.algorithm;
      task_description["object_index"] = task.task_index;
      task_description["start"] = task.t(0);
      task_description["end"] = task.t(-1);

      tmp["tasks"].push_back(task_description);
    }

    data.push_back(tmp);
  }

  return data;
}

enum class PlanStatus { failed, aborted, success, unplanned };

struct PlanResult {
  PlanResult() : status(PlanStatus::unplanned) {}
  PlanResult(PlanStatus _status) : status(_status){};
  PlanResult(PlanStatus _status, const Plan &_plan)
      : status(_status), plan(_plan){};

  PlanStatus status;
  Plan plan;
};

double
get_makespan_from_plan(const Plan &plan) {
  double max_time = 0.;
  for (const auto &robot_plan : plan) {
    const auto last_subpath = robot_plan.second.back();
    max_time = std::max({last_subpath.t(-1), max_time});
  }

  return max_time;
}

rai::Animation make_animation_from_plan(const Plan &plan) {
  rai::Animation A;
  for (const auto &p : plan) {
    for (const auto &path : p.second) {
      A.A.append(path.anim);
    }
  }

  return A;
}

arr get_robot_pose_at_time(const uint t, const Robot r,
                           const std::unordered_map<Robot, arr> &home_poses,
                           const Plan &plan) {
  if (plan.count(r) > 0) {
    for (const auto &part : plan.at(r)) {
      // std::cout <<part.t(0) << " " << part.t(-1) << std::endl;
      if (part.t(0) > t || part.t(-1) < t) {
        continue;
      }

      for (uint i = 0; i < part.t.N; ++i) {
        if (part.t(i) == t) {
          return part.path[i];
          // std::cout <<part.path[i] << std::endl;
        }
      }
    }
  }

  return home_poses.at(r);
}

std::string get_action_at_time_for_robot(const Plan &plan, const Robot r, const uint t) {
  if (plan.count(r) > 0) {
    for (const auto &part : plan.at(r)) {
      // std::cout <<part.t(0) << " " << part.t(-1) << std::endl;
      if (part.t(0) > t || part.t(-1) < t) {
        continue;
      }

      return part.name;
    }
  }

  return "none";
}

void set_full_configuration_to_time(rai::Configuration &C, const Plan plan, const uint t){
  const double makespan = get_makespan_from_plan(plan);
  // std::cout << t << std::endl;
  for (const auto &tp : plan) {
    const auto r = tp.first;
    const auto parts = tp.second;

    bool done = false;
    for (const auto &part : parts) {
      // std::cout <<part.t(0) << " " << part.t(-1) << std::endl;
      if (part.t(0) > t || part.t(-1) < t) {
        continue;
      }

      for (uint i = 0; i < part.t.N - 1; ++i) {
        if ((i == part.t.N - 1 && t == part.t(-1)) ||
            (i < part.t.N - 1 && (part.t(i) <= t && part.t(i + 1) > t))) {
          setActive(C, r);
          C.setJointState(part.path[i]);
          // std::cout <<part.path[i] << std::endl;
          done = true;

          // set bin picking things
          const auto task_index = part.task_index;
          const auto obj_name = STRING("obj" << task_index + 1);

          if (part.anim.frameNames.contains(obj_name)) {
            const auto pose =
                part.anim.X[uint(std::floor(t - part.anim.start))];
            arr tmp(1, 7);
            tmp[0] = pose[-1];
            C.setFrameState(tmp, {C[obj_name]});
          }
          break;
        }
      }

      if (done) {
        break;
      }
    }
  }
}

arr get_frame_pose_at_time(const rai::String &name, const Plan plan,
                           rai::Configuration &C, const uint t) {
  // set configuration to plan at time
  set_full_configuration_to_time(C, plan, t);

  // get pose and return it
  arr pose(7);
  pose = 0;
  pose = C[name]->getPose();
  // std::cout << name << " " << pose << std::endl;
  return pose;
}

std::unordered_map<std::string, arr>
get_frame_pose_at_time(const std::vector<rai::String> &names, const Plan plan,
                       rai::Configuration &C, const uint t) {
  set_full_configuration_to_time(C, plan, t);

  std::unordered_map<std::string, arr> poses;
  for (const auto &name : names) {
    arr pose(7);
    pose = 0;
    pose = C[name]->getPose();
    poses[name.p] = pose();
  }

  return poses;
}

std::vector<arr> get_frame_trajectory(const rai::String &name, Plan plan,
                                      rai::Configuration &C) {
  const uint makespan = 0;
  std::vector<arr> poses;
  for (uint i = 0; i < makespan; ++i) {
    const arr pose = get_frame_pose_at_time(name, plan, C, i);
    poses.push_back(pose);
  }
  return poses;
}

json make_scene_data(){json data; return data;}

void save_json_to_cbor_file(const json &data,
                            const std::string &file_path) {
  std::ofstream os(file_path, std::ios::out | std::ios::binary);
  std::vector<uint8_t> output_vector;
  json::to_cbor(data, output_vector);
  os.write((char *)&output_vector.data()[0], output_vector.size());
  os.close();
}

void save_json(const json &data, const std::string &file_path,
               const bool compressed = false) {
  spdlog::trace("Saving data to {}", file_path);
  if (compressed) {
    save_json_to_cbor_file(data, file_path);
  } else {
    std::ofstream f;
    f.open(file_path, std::ios_base::trunc);
    f << data;
    f.close();
  }
}

void export_plan(rai::Configuration C, const std::vector<Robot> &robots,
                 const std::unordered_map<Robot, arr> &home_poses,
                 const Plan &plan, const OrderedTaskSequence &seq,
                 const std::string base_folder, const uint iteration,
                 const uint computation_time) {
  spdlog::info("exporting plan");
  const bool write_compressed_json = global_params.compress_data;

  // make folder
  const std::string folder =
      global_params.output_path + base_folder + "/" + std::to_string(iteration) + "/";
  const int res = system(STRING("mkdir -p " << folder).p);
  (void)res;

  rai::Animation A = make_animation_from_plan(plan);

  // - add info
  // -- comp. time
  {
    std::ofstream f;
    f.open(folder + "comptime.txt", std::ios_base::trunc);
    f << computation_time / 1000.;
  }

  // -- makespan
  {
    std::ofstream f;
    f.open(folder + "makespan.txt", std::ios_base::trunc);
    f << A.getT();
  }

  // -- sequence
  {
    std::ofstream f;
    f.open(folder + "sequence.txt", std::ios_base::trunc);
    f << ordered_sequence_to_str(seq);
  }

  {
    const auto data = ordered_sequence_to_json(seq);
    save_json(data, folder + "sequence.json", write_compressed_json);
  }

  // {
  //   std::ofstream f;
  //   f.open(folder + "scene.urdf", std::ios_base::trunc);
  //   C.writeURDF(f);
  // }

  {
    // robots
    json data;
    for (const auto &e: home_poses){
      const auto r = e.first;
      const rai::String robot_base STRING(r << "base");

      data[robot_base.p] = C[robot_base]->getPose();
    }

    save_json(data, folder + "robot_pose.json", write_compressed_json);
  }

  {
    json data;
    for (const auto frame : C.frames) {
      if (frame->name.contains("obj")) {
        data[frame->name.p] = frame->getPose().vec();
      }
    }

    save_json(data, folder + "obj_initial_pose.json", write_compressed_json);
  }

  {
    json data;
    for (const auto frame : C.frames) {
      if (frame->name.contains("obj")) {
        data[frame->name.p] = frame->getShape().size;
      }
    }

    save_json(data, folder + "obj_sizes.json", write_compressed_json);
  }

  {
    json data;
    // goals
    for (const auto frame: C.frames){
      if (frame->name.contains("goal") && !frame->name.contains("marker")){
        json pose;
        pose["Position"] = frame->getRelativePosition().vec();
        pose["Quaternion"] = frame->getRelativeQuaternion().vec();
        data[frame->name.p] = pose;
      }
    }

    save_json(data, folder + "obj_goals.json", write_compressed_json);
  }

  {
    std::map<rai::String, arr> objs;
    for (const auto frame: C.frames){
      if (frame->name.contains("obj")){
        objs[frame->name] = arr(0, 7);
      }
    }

    const double makespan = get_makespan_from_plan(plan);
    for (uint t = 0; t < A.getT(); ++t) {
      // std::cout << t << std::endl;
      for (const auto &tp : plan) {
        const auto r = tp.first;
        const auto parts = tp.second;

        bool done = false;
        for (const auto &part : parts) {
          // std::cout <<part.t(0) << " " << part.t(-1) << std::endl;
          if (part.t(0) > t || part.t(-1) < t) {
            continue;
          }

          for (uint i = 0; i < part.t.N - 1; ++i) {
            if ((i == part.t.N-1 && t == part.t(-1)) ||
              (i < part.t.N-1 && (part.t(i) <= t && part.t(i + 1) > t))) {
              setActive(C, r);
              C.setJointState(part.path[i]);
              // std::cout <<part.path[i] << std::endl;
              done = true;

              // set bin picking things
              const auto task_index = part.task_index;
              const auto obj_name = STRING("obj" << task_index + 1);

              if (part.anim.frameNames.contains(obj_name)) {
                const auto pose =
                    part.anim.X[uint(std::floor(t - part.anim.start))];
                arr tmp(1, 7);
                tmp[0] = pose[-1];
                C.setFrameState(tmp, {C[obj_name]});
              }
              break;
            }
          }

          if (done) {
            break;
          }
        }
      }

      for (const auto &obj: objs){
        objs[obj.first].append(C[obj.first]->getPose());
      }
    }

    json data;
    for (const auto &obj: objs){
      std::vector<arr> tmp;
      for (uint i=0; i<obj.second.d0; ++i) tmp.push_back(obj.second[i]);
      data[obj.first.p] = tmp;
    }

    save_json(data, folder + "obj_poses.json", write_compressed_json);
  }

  // -- plan
  {
    std::ofstream f;
    f.open(folder + "plan.txt", std::ios_base::trunc);

    for (const auto &per_robot_plan : plan) {
      const auto robot = per_robot_plan.first;
      const auto tasks = per_robot_plan.second;

      f << robot << ": ";
      for (const auto &task : tasks) {
        f << task.name << "(" << task.algorithm << ")"
          << " " << task.task_index << ", " << task.t(0) << ", " << task.t(-1)
          << "; ";
      }
      f << std::endl;
    }
  }

  {
    json data = get_plan_as_json(plan);
    save_json(data, folder + "plan.json", write_compressed_json);
  }

  // -- compute times
  {
    std::ofstream f;
    f.open(folder + "computation_times.txt", std::ios_base::trunc);
    for (const auto &per_robot_plan : plan) {
      const auto robot = per_robot_plan.first;
      const auto tasks = per_robot_plan.second;

      f << robot << ": ";
      for (const auto &task : tasks) {
        f << task.name << "(" << task.algorithm << ")"
          << " " << task.task_index << ", " << task.stats.total_compute_time
          << ", " << task.stats.rrt_compute_time << ", "
          << task.stats.rrt_plan_time << ", "
          << task.stats.rrt_coll_time << ", "
          << task.stats.rrt_nn_time << ", "
          << task.stats.rrt_smoothing_time << ", "
          << task.stats.rrt_shortcut_time << ", "
          << task.stats.komo_compute_time << "; ";
      }
      f << std::endl;
    }
  }

  // -- actual path
  {
    std::ofstream f;
    f.open(folder + "robot_controls.txt", std::ios_base::trunc);
    arr path(A.getT(), home_poses.at(robots[0]).d0 * robots.size());
    for (uint i = 0; i < A.getT(); ++i) {
      uint offset = 0;
      for (uint j = 0; j < robots.size(); ++j) {
        const arr pose = get_robot_pose_at_time(i, robots[j], home_poses, plan);
        for (uint k = 0; k < pose.N; ++k) {
          path[i](k + offset) = pose(k);
        }
        offset += pose.N;
      }
    }

    f << path;
  }

  {
    std::vector<rai::String> frame_names;
    for (uint j = 0; j < robots.size(); ++j) {
      const rai::String ee_frame_name =
          STRING("" << robots[j].prefix << "pen_tip");
      frame_names.push_back(ee_frame_name);
    }

    std::vector<rai::String> obj_names;
    for (const auto frame : C.frames) {
      if (frame->name.contains("obj")) {
        frame_names.push_back(frame->name);
        obj_names.push_back(frame->name);
      }
    }

    std::unordered_map<std::string, std::vector<arr>> frame_poses;

    const double makespan = get_makespan_from_plan(plan);
    for (uint t = 0; t < A.getT(); ++t) {
      const auto poses = get_frame_pose_at_time(frame_names, plan, C, t);
      for (const auto &name_pose : poses) {
        frame_poses[name_pose.first].push_back(name_pose.second);
      }
    }

    json all_robot_data;
    // arr path(A.getT(), home_poses.at(robots[0]).d0 * robots.size());
    for (uint j = 0; j < robots.size(); ++j) {
      json robot_data;
      robot_data["name"] = robots[j].prefix;
      robot_data["type"] =  robot_type_to_string(robots[j].type);
      robot_data["ee_type"] = ee_type_to_string(robots[j].ee_type);

      for (uint i = 0; i < A.getT(); ++i) {
        spdlog::trace("exporting traj step {}", i);

        spdlog::trace("symbol at time {}", i);
        const arr pose = get_robot_pose_at_time(i, robots[j], home_poses, plan);
        
        spdlog::trace("symbol at time {}", i);
        const rai::String ee_frame_name = STRING("" << robots[j].prefix << "pen_tip");
        const arr ee_pose = frame_poses[ee_frame_name.p][i];

        spdlog::trace("symbol at time {}", i);
        const std::string current_action = get_action_at_time_for_robot(plan, robots[j], i);
        // const std::string current_primitive = get_primitive_at_time_for_robot(plan, robots[j], i);
        // const std::string current_primitive = primitive_type_to_string(primitve);
        // const std::string current_primitive = "pick";

        json step_data;
        step_data["joint_state"] = pose;
        step_data["ee_pos"] = ee_pose({0,2});
        step_data["ee_quat"] = ee_pose({3,6});

        step_data["action"] = current_action;
        // step_data["primitive"] = current_action;

        robot_data["steps"].push_back(step_data);
      }

      all_robot_data.push_back(robot_data);
    }

    // all objs    
    json all_obj_data;
    for (const auto &obj: obj_names){
      json obj_data;
      obj_data["name"] = obj;
      const auto poses = frame_poses[obj.p];
      for (uint i=0; i<poses.size(); ++i){
        json step_data;
        step_data["pos"] = poses[i]({0,2});
        step_data["quat"] = poses[i]({3,6});
        obj_data["steps"].push_back(step_data);
      }

      all_obj_data.push_back(obj_data);
    }

    json data;
    data["robots"] = all_robot_data;
    data["objs"] = all_obj_data;

    data["metadata"]["makespan"] = makespan;
    data["metadata"]["folder"] = folder;

    data["metadata"]["num_robots"] = 0;   
    data["metadata"]["num_objects"] = 0;

    save_json(data, folder + "trajectory.json", write_compressed_json);
  }

  {
    // arr path(A.getT(), home_poses.at(robots[0]).d0 * robots.size());
    std::unordered_map<Robot, std::vector<arr>> paths;
    for (uint i = 0; i < A.getT(); ++i) {
      for (uint j = 0; j < robots.size(); ++j) {
        const arr pose = get_robot_pose_at_time(i, robots[j], home_poses, plan);
        paths[robots[j]].push_back(pose);
      }
    }

    json data;
    for (const auto &element: paths){
      data[element.first.prefix] = element.second;
    }

    save_json(data, folder + "robot_controls.json", write_compressed_json);
  }
}

void visualize_plan(rai::Configuration &C, const Plan &plan, const bool display = true, const std::string image_path = "") {
  spdlog::info("Showing plan");

  const double makespan = get_makespan_from_plan(plan);
  spdlog::info("Plan duration: {}", makespan);

  const auto initial_frame_state = C.getFrameState();

  arr framePath(makespan, C.frames.N, 7);

  // we can not simly use the animations that are in the path
  // since they do not contain all the frames.
  // Thus, we hve to retrieve the correct part, find the right time, and then set the
  // given configuration to this state.
  for (uint t = 0; t < makespan; ++t) {
    for (const auto &tp : plan) {
      const auto r = tp.first;
      const auto parts = tp.second;

      bool done = false;
      for (const auto &part : parts) {
        // std::cout <<part.t(0) << " " << part.t(-1) << std::endl;
        if (part.t(0) > t || part.t(-1) < t) {
          continue;
        }

        for (uint i = 0; i < part.t.N; ++i) {
          if ((i == part.t.N-1 && t == part.t(-1)) ||
              (i < part.t.N-1 && (part.t(i) <= t && part.t(i + 1) > t))) {
            setActive(C, r);
            C.setJointState(part.path[i]);
            // std::cout <<part.path[i] << std::endl;
            done = true;

            // set bin picking things
            const auto task_index = part.task_index;
            const auto obj_name = STRING("obj" << task_index + 1);

            if (part.anim.frameNames.contains(obj_name)) {
              const auto pose =
                  part.anim.X[uint(std::floor(t - part.anim.start))];
              arr tmp(1, 7);
              tmp[0] = pose[-1];
              C.setFrameState(tmp, {C[obj_name]});
            }
            break;
          }
        }
        
        if (done) {
          framePath[t] = C.getFrameState();
          break;
        }
      }
    }
  }

  spdlog::debug("Finished assembling framepath.");

  rai::ConfigurationViewer Vf;
  Vf.offscreen = !display;

  Vf.setConfiguration(C, "\"Real World\"", false);
  Vf.setPath(framePath, NULL, false, false);

  Vf.drawFrameLines = false;

  // if we do not set the duration to a short time for the case where we only
  // want to export images, it takes too long.
  double duration = 0.01 * makespan;
  if (!display){
    duration = 0.00001;
  }

  if (image_path != "") {
    Vf.playVideo(false, duration, image_path.c_str());
  }
  else {
    Vf.playVideo(false, duration);
  }

  // making sure that this does not alter the configuration.
  C.setFrameState(initial_frame_state);
}