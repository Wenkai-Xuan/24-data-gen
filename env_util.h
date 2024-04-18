#pragma once

#include "json/json.h"
#include <Kin/kin.h>

#include "types.h"
#include "plan.h"
#include <PlanningSubroutines/ConfigurationProblem.h>

using json = nlohmann::json;

void setActive(rai::Configuration &C,
               const std::vector<std::string> &prefixes) {
  FrameL roots;
  for (const auto &prefix : prefixes) {
    roots.append(C.getFrame(STRING(prefix << "base")));
  }
  C.selectJointsBySubtrees(roots);
}

void setActive(rai::Configuration &C, const std::string &prefix) {
  setActive(C, std::vector<std::string>{prefix});
}

void setActive(rai::Configuration &C, const Robot &r){
  setActive(C, r.prefix);
}

void setActive(rai::Configuration &C, const std::vector<Robot> &robots){
  std::vector<std::string> robot_prefixes;
  for (auto r: robots){robot_prefixes.push_back(r.prefix);}
  setActive(C, robot_prefixes);
}

void setRobotJointState() {}

std::vector<Robot>
make_robot_environment_from_config(rai::Configuration &C,
                                   const std::string &config_file_path) {
  C.addFile("./in/robots/floor.g");

  std::ifstream ifs(config_file_path);
  json jf = json::parse(ifs);

  uint cnt = 0;
  std::stringstream ss;

  std::vector<Robot> robots;

  for (const auto &item : jf["robots"].items()) {
    ss.clear();
    ss << item.value()["base_pos"];
    arr base_pos;
    base_pos.read(ss);

    ss.clear();
    arr base_quat;
    ss << item.value()["base_quat"];
    base_quat.read(ss);

    std::string robot = item.value()["type"];
    rai::Frame *a;
    EndEffectorType ee;
    if (robot == "ur5_gripper") {
      a = C.addFile("./in/robots/ur5.g");
      ee = EndEffectorType::two_finger;
    } else if (robot == "ur5_vacuum") {
      a = C.addFile("./in/robots/ur5_vacuum.g");
      ee = EndEffectorType::vacuum;
    } else if (robot == "ur5_vacuum") {
      a = C.addFile("./in/robots/ur5_pen.g");
      ee = EndEffectorType::vacuum;
    } else if (robot == "franka"){
      a = C.addFile("./in/robots/franka.g");
      ee = EndEffectorType::two_finger;
    } else if (robot == "kuka"){
      a = C.addFile("./in/robots/kuka.g");
      ee = EndEffectorType::two_finger;
    }
    else{
      spdlog::error("Invalid robot type");
      return {};
    }

    C.reconfigureRoot(a, true);
    a->linkFrom(C["table"]);

    const rai::String prefix = STRING('a' << cnt << '_');
    a->prefixSubtree(prefix);

    const rai::String agentBase = STRING(prefix << "base");
    C[agentBase]->setRelativePosition(base_pos);
    C[agentBase]->setQuaternion(base_quat);

    // check if home_pose is set, otherwise use default
    setActive(C, std::string(prefix.p));
    arr home_pose = C.getJointState();

    if (item.value().contains("home_pose")) {
      ss.clear();
      ss << item.value()["home_pose"];
      arr state;
      state.read(ss);

      assert(state.d0 == C.getJointState().N);
      home_pose = state;
    }

    if (item.value().contains("start_pose")) {
      ss.clear();
      ss << item.value()["start_pose"];
      arr state;
      state.read(ss);

      assert(state.d0 == C.getJointState().N);
      C.setJointState(state);
    }

    // check if speed is set, otherwise use default
    double vmax = 0.05;
    if (item.value().contains("vmax")) {
      vmax = item.value()["vmax"];
    }

    ConfigurationProblem cp(C);
    cp.activeOnly = true;
    const auto feasible = cp.query({}, false)->isFeasible;

    if (!feasible){
      spdlog::error("Seeting up configuration: Robot {} is in collision.", cnt);
    }

    robots.push_back(Robot(prefix.p, RobotType::ur5, vmax));
    robots.back().home_pose = home_pose;
    robots.back().ee_type = ee;

    ++cnt;
  }

  return robots;
}

void add_objects_from_config(rai::Configuration &C,
                             const std::string &config_file_path) {
  std::ifstream ifs(config_file_path);
  json jf = json::parse(ifs);

  uint cnt = 0;
  std::stringstream ss;

  for (const auto &item : jf["objects"].items()) {
    ss.clear();
    ss << item.value()["shape"];
    arr shape;
    shape.read(ss);

    ss.clear();
    ss << item.value()["start_pos"];
    arr base_pos;
    base_pos.read(ss);

    ss.clear();
    ss << item.value()["start_quat"];
    arr base_quat;
    base_quat.read(ss);

    ss.clear();
    ss << item.value()["goal_pos"];
    arr goal_pos;
    goal_pos.read(ss);

    ss.clear();
    ss << item.value()["goal_quat"];
    arr goal_quat;
    goal_quat.read(ss);

    auto *obj = C.addFrame(STRING("obj" << cnt + 1), "table");

    // randomize color
    arr col(3);
    rndUniform(col, 0, 1);

    obj->setShape(rai::ST_box, {shape(0), shape(1), shape(2), 0.01});
    obj->setContact(1);
    obj->setJoint(rai::JT_rigid);
    obj->setRelativePosition(base_pos);
    obj->setRelativeQuaternion(base_quat);
    obj->setColor({col(0), col(1), col(2)});

    auto *marker = C.addFrame("goal_marker", obj->name);
    marker->setShape(rai::ST_marker, {0.1});
    marker->setContact(0.);

    auto *goal = C.addFrame(STRING("goal" << cnt + 1), "table");

    goal->setShape(rai::ST_box, {shape(0), shape(1), shape(2), 0.01});
    goal->setContact(0);
    goal->setJoint(rai::JT_rigid);
    goal->setRelativePosition(goal_pos);
    goal->setRelativeQuaternion(goal_quat);
    // goal->setColor({0, 0, 0, 0.5});
    goal->setColor({col(0), col(1), col(2), 0.5});

    // check if something is in collision
    ConfigurationProblem cp(C);
    const auto res = cp.query({}, false);
    if (!res->isFeasible){
      spdlog::error("Initial configuration not feasible. Obj {} is in collision.", cnt);
    }

    ++cnt;
  }

  C.stepFcl();
}
// TODO: fill this in
bool check_configuration_feasibility(const rai::Configuration & C){
  return true;
}

std::vector<Robot>
tub_lab_setting(rai::Configuration &C) {
  auto *base = C.addFrame("world", "");
  base->setShape(rai::ST_marker, {0.001});
  base->setPosition({0., 0., .5});
  base->setContact(0.);

  C.addFile("./in/robots/table.g");

  const arrA basePos = {{-.4, -.3, 0.00}, {.4, -.3, 0.0}, {.0, .6, 0.15}};

  std::vector<Robot> robots;

  for (uint i = 0; i < 2; i++) {
    auto *a = C.addFile("./in/robots/franka.g");
    C.reconfigureRoot(a, true);
    a->linkFrom(C["table"]);

    const rai::String prefix = STRING('a' << i << '_');
    a->prefixSubtree(prefix);

    const rai::String agentBase = STRING(prefix << "base");
    C[agentBase]->setRelativePosition(basePos(i));

    robots.push_back(Robot(prefix.p, RobotType::pandas, 0.05));
    robots.back().home_pose = C.getJointState();
    robots.back().ee_type = EndEffectorType::two_finger;

  }

  return robots;
}

std::vector<Robot>
more_robots(rai::Configuration &C, const uint n = 2) {
  auto *base = C.addFrame("world", "");
  base->setShape(rai::ST_marker, {0.001});
  base->setPosition({0., 0., .5});
  base->setContact(0.);

  C.addFile("./in/robots/table.g");

  arrA basePos = {
      {-.5, -.35, 0.00}, {.5, -.35, 0.0}, {-.5, .55, 0.}, {.5, .55, 0.0}};
  arrA baseQuat = {
      {0.924, 0, 0, 0.383},
      {0.383, 0, 0, 0.924},
      {0.924, 0, 0, -0.383},
      {-0.383, 0, 0, 0.924},
  };

  std::vector<Robot> robots;
  for (uint i = 0; i < n; i++) {
    auto *a = C.addFile("./in/robots/franka.g");
    C.reconfigureRoot(a, true);
    a->linkFrom(C["table"]);

    const rai::String prefix = STRING('a' << i << '_');
    a->prefixSubtree(prefix);

    const rai::String agentBase = STRING(prefix << "base");
    // const rai::String agentBase = STRING(prefix<<"base");
    // C[agentBase]->setPosition(basePos(i));
    C[agentBase]->setRelativePosition(basePos(i));
    C[agentBase]->setQuaternion(baseQuat(i));

    setActive(C, std::string(prefix.p));
    arr state = C.getJointState();
    state(1) -= 0.25;
    state(3) += 0.25;
    C.setJointState(state);

    robots.push_back(Robot(prefix.p, RobotType::pandas, 0.05));
    robots.back().home_pose = C.getJointState();
    robots.back().ee_type = EndEffectorType::two_finger;
  }

  return robots;
}

std::vector<Robot>
opposite_robot_configuration(rai::Configuration &C){
  C.addFile("./in/robots/floor.g");

  const arrA basePos = {{-.5, -.1, 0.00}, {.5, .1, 0.0}, {.0, .6, 0.15}};

  const arrA baseQuat = {
      {1, 0, 0, 0},
      {0, 0, 0, 1},
      {0.924, 0, 0, -0.383},
      {-0.383, 0, 0, 0.924},
  };

  std::vector<Robot> robots;
  for (uint i = 0; i < 2; i++) {
    auto *a = C.addFile("./in/robots/franka.g");
    C.reconfigureRoot(a, true);
    a->linkFrom(C["table"]);

    const rai::String prefix = STRING('a' << i << '_');
    a->prefixSubtree(prefix);

    const rai::String agentBase = STRING(prefix << "base");
    C[agentBase]->setRelativePosition(basePos(i));
    C[agentBase]->setQuaternion(baseQuat(i));

    setActive(C, std::string(prefix.p));
    arr state = C.getJointState();
    state(1) -= 0.25;
    // state(3) += 0.25;
    C.setJointState(state);

    robots.push_back(Robot(prefix.p, RobotType::pandas, 0.05));
    robots.back().home_pose = C.getJointState();    
    robots.back().ee_type = EndEffectorType::two_finger;
  }

  return robots;
}

std::vector<Robot>
side_by_side_robot_configuration(rai::Configuration &C){
  C.addFile("./in/robots/floor.g");

  const arrA basePos = {{-.4, -.3, 0.00}, {.4, -.3, 0.0}, {.0, .6, 0.15}};

  std::vector<Robot> robots;
  for (uint i = 0; i < 2; i++) {
    auto *a = C.addFile("./in/robots/franka.g");
    C.reconfigureRoot(a, true);
    a->linkFrom(C["table"]);

    const rai::String prefix = STRING('a' << i << '_');
    a->prefixSubtree(prefix);

    const rai::String agentBase = STRING(prefix << "base");
    C[agentBase]->setRelativePosition(basePos(i));

    robots.push_back(Robot(prefix.p, RobotType::pandas, 0.05));
    robots.back().home_pose = C.getJointState();
    robots.back().ee_type = EndEffectorType::two_finger;
  }

  return robots;
}

std::vector<Robot> make_configuration_from_base_pose_and_quat(
    rai::Configuration &C, const arrA base_pos, const arrA base_quat,
    const bool two_finger_gripper) {
  assert(base_pos.d0 == base_quat.d0);
  const uint N = base_pos.d0;

  std::vector<Robot> robots;

  for (uint i = 0; i < N; ++i) {
    rai::Frame *a;
    EndEffectorType ee;
    if (two_finger_gripper) {
      a = C.addFile("./in/robots/ur5.g");
      ee = EndEffectorType::two_finger;
    } else {
      a = C.addFile("./in/robots/ur5_vacuum.g");
      ee = EndEffectorType::vacuum;
    }
    // auto *a = C.addFile("./in/franka.g");
    C.reconfigureRoot(a, true);
    a->linkFrom(C["table"]);

    const rai::String prefix = STRING('a' << i << '_');
    a->prefixSubtree(prefix);

    const rai::String agentBase = STRING(prefix << "base");
    C[agentBase]->setRelativePosition(base_pos(i));
    C[agentBase]->setQuaternion(base_quat(i));

    setActive(C, std::string(prefix.p));
    arr state = C.getJointState();
    // state(1) -= .5;
    // state(3) += 0.25;
    C.setJointState(state);

    robots.push_back(Robot(prefix.p, RobotType::ur5, 0.05));
    robots.back().home_pose = C.getJointState();
    robots.back().ee_type = ee;
  }

  return robots;
}

std::vector<Robot>
single_robot_configuration(rai::Configuration &C, const bool two_finger_gripper=true){
  C.addFile("./in/robots/floor.g");

  const arrA basePos = {{-.4, -.3, 0.00}};

    const arrA baseQuat = {
      {1, 0, 0, 1}
  };

  return make_configuration_from_base_pose_and_quat(C, basePos, baseQuat, two_finger_gripper);
}

std::vector<Robot>
two_robot_configuration(rai::Configuration &C, const bool two_finger_gripper=true){
  C.addFile("./in/robots/floor.g");

  const arrA basePos = {{-.4, -.3, 0.00}, {.4, -.3, 0.0}};

    const arrA baseQuat = {
      {1, 0, 0, 1},
      {1, 0, 0, 1}
  };

  return make_configuration_from_base_pose_and_quat(C, basePos, baseQuat, two_finger_gripper);
}

std::vector<Robot>
opposite_three_robot_configuration(rai::Configuration &C, const bool two_finger_gripper=true){
  C.addFile("./in/robots/floor.g");

  const arrA basePos = {{-.4, -.3, 0.00}, {.4, -.3, 0.0}, {.0, .6, 0.0}};

  const arrA baseQuat = {
      {1, 0, 0, 1},
      {1, 0, 0, 1},
      {-1, 0, 0, 1},
  };

  return make_configuration_from_base_pose_and_quat(C, basePos, baseQuat,
                                                    two_finger_gripper);
}

void random_objects(rai::Configuration &C, const uint N, const double width=.5){  
  for (uint i=0; i<N; ++i){
    auto *obj = C.addFrame(STRING("obj"<<i+1), "table");

    arr shape(2);
    rndUniform(shape, 0.02, 0.04);
    shape(0) *= 2;

    obj->setShape(rai::ST_box, {shape(0), shape(1), 0.06, 0.01});
    obj->setContact(1.);
    obj->setJoint(rai::JT_rigid);

    while(true){
      arr rnd(2);
      rndUniform(rnd, 0, 1);
      rnd(0) = (rnd(0) - 0.5) * width;
      rnd(1) = (rnd(1) - 0.3) * 0.7;

      obj->setPosition({rnd(0), rnd(1), 0.66});

      ConfigurationProblem cp(C);
      if (cp.query({}, false)->isFeasible){
        break;
      } 
    }

    auto *goal = C.addFrame(STRING("goal"<<i+1), "table");

    goal->setShape(rai::ST_box, {shape(0), shape(1), 0.06, 0.01});
    goal->setContact(1.);
    goal->setColor({0, 0, 0, 0.5});
    goal->setJoint(rai::JT_rigid);
    
    while(true){
      arr rnd(2);
      rndUniform(rnd, 0, 1);
      // rnd(0) = (rnd(0) - 0.5) * 0.5;
      // rnd(1) = (rnd(1) - 0.3) * 0.5;

      rnd(0) = (rnd(0) - 0.5) * width;
      rnd(1) = (rnd(1) - 0.3) * 1;

      goal->setPosition({rnd(0), rnd(1), 0.66});

      ConfigurationProblem cp(C);
      if (cp.query({}, false)->isFeasible){
        break;
      } 
    }

    goal->setContact(1.);
  }

  for (auto f: C.frames){
    if (f->name.contains("goal")){
      f->setContact(0);
    }
  }
}

void line(rai::Configuration &C, const uint N, const double width = 2.) {
  for (uint i = 0; i < N; ++i) {
    auto *obj = C.addFrame(STRING("obj" << i + 1), "table");

    arr shape(2);
    rndUniform(shape, 0.02, 0.04);
    shape(0) *= 2;

    obj->setShape(rai::ST_box, {shape(0), shape(1), 0.06, 0.01});
    obj->setContact(1.);
    obj->setJoint(rai::JT_rigid);
    obj->setPosition({width / (N - 1) * i - width / 2, 0.3, 0.66});

    auto *goal = C.addFrame(STRING("goal" << i + 1), "table");

    goal->setShape(rai::ST_box, {shape(0), shape(1), 0.06, 0.01});
    goal->setContact(1.);
    goal->setColor({0, 0, 0, 0.5});
    goal->setJoint(rai::JT_rigid);
    goal->setPosition({width / (N - 1) * i - width / 2, 0., 0.66});

    goal->setContact(1.);
  }

  for (auto f : C.frames) {
    if (f->name.contains("goal")) {
      f->setContact(0);
    }
  }
}

void shuffled_line(rai::Configuration &C, const uint N,
                   const double width = 2., const bool random_size=true) {
  const double heigth_offset = 0.6 - 0.05 + 0.05 / 2 + 0.001;
  const double height = 0.06;

  std::vector<uint> tmp;
  for (uint q = 0; q < N; ++q) {
    tmp.push_back(q);
  }

  // Shuffle the vector
  std::random_shuffle(tmp.begin(), tmp.end());

  for (uint i = 0; i < N; ++i) {
    auto *obj = C.addFrame(STRING("obj" << i + 1), "table");

    arr shape(2);
    rndUniform(shape, 0.02, 0.06);
    shape(1) *= 2;

    if (!random_size){
      shape(0) = 0.04;
      shape(1) = 0.2;
    }

    obj->setShape(rai::ST_box, {shape(0), shape(1), height, 0.01});
    obj->setContact(1.);
    obj->setJoint(rai::JT_rigid);
    obj->setPosition(
        {width / (N - 1) * i - width / 2, 0.3, height / 2 + heigth_offset});
        obj->setQuaternion( {1, 0, 0, 1});

    auto *marker = C.addFrame("goal_marker", obj->name);
    marker->setShape(rai::ST_marker, {0.1});
    marker->setContact(0.);

    auto *goal = C.addFrame(STRING("goal" << i + 1), "table");

    goal->setShape(rai::ST_box, {shape(0), shape(1), height, 0.01});
    goal->setContact(1.);
    goal->setColor({0, 0, 0, 0.5});
    goal->setJoint(rai::JT_rigid);
    goal->setPosition(
        {width / (N - 1) * tmp[i] - width / 2, 0., height / 2 + heigth_offset});

    goal->setContact(1.);
  }

  for (auto f : C.frames) {
    if (f->name.contains("goal")) {
      f->setContact(0);
    }
  }
}

void big_objs(rai::Configuration &C, const uint N){  
  const double width = 2.;
  const double heigth_offset = 0.6 - 0.05 + 0.05 / 2;
  
  std::vector<uint> tmp;
  for (uint q = 0; q < N; ++q) {
    tmp.push_back(q);
  }

  // Shuffle the vector
  std::random_shuffle(tmp.begin(), tmp.end());

  for (uint i=0; i<N; ++i){
    auto *obj = C.addFrame(STRING("obj"<<i+1), "table");

    arr shape(2);
    rndUniform(shape, 0.04, 0.1);
    shape(0) = shape(0) * 1.5;

    const double height = 0.06;

    std::cout << shape << std::endl;

    obj->setShape(rai::ST_box, {shape(0), shape(1), height, 0.01});
    obj->setContact(1.);
    obj->setJoint(rai::JT_rigid);
    obj->setPosition({width / (N-1) * i - 1, 0.3, height / 2 + heigth_offset});

    auto *goal = C.addFrame(STRING("goal"<<i+1), "table");

    goal->setShape(rai::ST_box, {shape(0), shape(1), 0.06, 0.01});
    goal->setContact(1.);
    goal->setColor({0, 0, 0, 0.5});
    goal->setJoint(rai::JT_rigid);
    goal->setPosition({width / (N-1) * tmp[i] - 1, 0., height / 2 + heigth_offset});

    goal->setContact(1.);
  }

  for (auto f: C.frames){
    if (f->name.contains("goal")){
      f->setContact(0);
    }
  }
}

void pick_and_place(rai::Configuration &C) {
  auto *base = C.addFrame("world", "");
  base->setShape(rai::ST_marker, {0.001});
  base->setPosition({0., 0., .5});
  base->setContact(0.);

  C.addFile("./in/robots/table_pick_place.g");

  const arrA basePos = {{-.5, -.1, 0.00}, {.5, .1, 0.0}, {.0, .6, 0.15}};

  const arrA baseQuat = {
      {1, 0, 0, 0},
      {0, 0, 0, 1},
      {0.924, 0, 0, -0.383},
      {-0.383, 0, 0, 0.924},
  };

  for (uint i = 0; i < 2; i++) {
    auto *a = C.addFile("./in/robots/franka.g");
    C.reconfigureRoot(a, true);
    a->linkFrom(C["table"]);

    const rai::String prefix = STRING('a' << i << '_');
    a->prefixSubtree(prefix);

    const rai::String agentBase = STRING(prefix << "base");
    C[agentBase]->setRelativePosition(basePos(i));
    C[agentBase]->setQuaternion(baseQuat(i));

    setActive(C, std::string(prefix.p));
    arr state = C.getJointState();
    state(1) -= 0.25;
    // state(3) += 0.25;
    C.setJointState(state);
  }

  // C.watch(true);
}

std::unordered_map<Robot, arr> get_robot_home_poses(const std::vector<Robot> &robots) {
  std::unordered_map<Robot, arr> poses;
  for (const auto &r : robots) {
    poses[r] = r.home_pose;
  }

  return poses;
}