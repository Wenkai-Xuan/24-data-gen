#pragma once

#include "spdlog/spdlog.h"

#include <Kin/featureSymbols.h>

#include <Kin/F_collisions.h>

#include "common/util.h"
#include "planners/plan.h"
#include "planners/prioritized_planner.h"

#include "samplers/pick_constraints.h"

// TODO: unify the two things
// - reduce code duplication of actual solver and subproblem
std::vector<arr> solve_subproblem(rai::Configuration &C, Robot r1, Robot r2,
                                  rai::String obj, rai::String goal) {
  spdlog::info("Solving subproblem for handover");
  std::unordered_map<Robot, FrameL> robot_frames;
  for (const auto &r : {r1, r2}) {
    robot_frames[r] = get_robot_joints(C, r);
  }

  // C.watch(true);
  OptOptions options;
  // options.allowOverstep = true;
  options.nonStrictSteps = 50;
  options.damping = 10;
  options.wolfe = 0.01;

  // // options.stopIters = 200;
  // options.maxStep = 0.5;

  // options.maxStep = 1;

  ConfigurationProblem cp(C);
  const arr obj_pos = C[obj]->getPosition();
  const arr goal_pos = C[goal]->getPosition();

  const arr r1_pos = C[STRING(r1 << "base")]->getPosition();
  const arr r2_pos = C[STRING(r2 << "base")]->getPosition();

  const auto link_to_frame = STRING("table");

  KOMO komo;
  // komo.verbose = 5;
  komo.verbose = 0;
  komo.setModel(C, true);
  // komo.animateOptimization = 5;

  // komo.world.fcl()->deactivatePairs(pairs);
  // komo.pathConfig.fcl()->stopEarly = true;

  komo.setDiscreteOpt(2);

  // komo.add_collision(true, .05, 1e1);
  komo.add_collision(true, .1, 1e1);

  komo.add_jointLimits(true, 0., 1e1);
  komo.addSquaredQuaternionNorms();

  const auto r1_pen_tip = STRING(r1 << r1.ee_frame_name);
  const auto r2_pen_tip = STRING(r2 << r2.ee_frame_name);

  const double r1_z_rot = C[STRING(r1 << "base")]->get_X().rot.getEulerRPY()(2);
  const double r2_z_rot = C[STRING(r2 << "base")]->get_X().rot.getEulerRPY()(2);

  const double r1_obj_angle =
      std::atan2(obj_pos(1) - r1_pos(1), obj_pos(0) - r1_pos(0)) - r1_z_rot;
  const double r1_r2_angle =
      std::atan2(r2_pos(1) - r1_pos(1), r2_pos(0) - r1_pos(0)) - r1_z_rot;
  const double r2_r1_angle =
      std::atan2(r1_pos(1) - r2_pos(1), r1_pos(0) - r2_pos(0)) - r2_z_rot;

  Skeleton S = {
      // {1., 2., SY_touch, {r1_pen_tip, obj}},
      {1., 2, SY_stable, {r1_pen_tip, obj}},
      // {2., -1., SY_touch, {r2_pen_tip, obj}},
      // {2., 3., SY_stable, {r2_pen_tip, obj}},
      // {3., -1, SY_poseEq, {obj, goal}},
      // {3., -1, SY_positionEq, {obj, goal}}
      // {3., -1, SY_stable, {obj, goal}},
  };

  komo.setSkeleton(S);

  const double offset = 0.1;
  komo.addObjective({2., 2.}, FS_distance, {link_to_frame, obj}, OT_ineq, {1e0},
                    {-offset});

  // komo.addObjective({1., 1.}, FS_aboveBox, {obj, r1_pen_tip}, OT_ineq, {1e2},
  // {0.0, 0.0, 0.0, 0.0}); komo.addObjective({2., 2.}, FS_aboveBox, {obj,
  // r2_pen_tip}, OT_ineq, {1e2}, {0.1, 0.1, 0.1, 0.1});

  komo.addObjective({1., 1.}, FS_positionDiff, {r1_pen_tip, obj}, OT_sos,
                    {1e0});
  komo.addObjective({2., 2.}, FS_positionDiff, {r2_pen_tip, obj}, OT_sos,
                    {1e0});

  komo.addObjective({1., 1.}, FS_insideBox, {r1_pen_tip, obj}, OT_ineq, {5e1});
  komo.addObjective({2., 2.}, FS_insideBox, {r2_pen_tip, obj}, OT_ineq, {5e1});

  // const double margin = 0.1;
  // komo.addObjective({1., 1.}, FS_positionDiff, {r1_pen_tip, STRING(obj)},
  //                   OT_ineq, {-1e1}, {-margin, -margin, -margin});

  // komo.addObjective({1., 1.}, FS_positionDiff, {r1_pen_tip, STRING(obj)},
  //             OT_ineq, {1e1}, {margin, margin, margin});

  // komo.addObjective({2., 2.}, FS_positionDiff, {r2_pen_tip, STRING(obj)},
  //                   OT_ineq, {-1e1}, {-margin, -margin, -margin});

  // komo.addObjective({2., 2.}, FS_positionDiff, {r2_pen_tip, STRING(obj)},
  //                   OT_ineq, {1e1}, {margin, margin, margin});

  komo.addObjective({1., 1.}, FS_scalarProductZZ, {obj, r1_pen_tip}, OT_sos,
                    {1e1}, {-1.});

  komo.addObjective({2., 2.}, FS_scalarProductZZ, {obj, r2_pen_tip}, OT_sos,
                    {1e1}, {-1.});

  // komo.addObjective({1.}, FS_scalarProductYX, {obj, r1_pen_tip},
  //                   OT_sos, {1e0}, {1.});

  // komo.addObjective({2.}, FS_scalarProductYX, {obj, r2_pen_tip},
  //                   OT_sos, {1e0}, {1.});

  // identify long axis
  if (C[obj]->shape->size(0) > C[obj]->shape->size(1)) {
    // x longer than y
    spdlog::info("Trying to grab along x-axis");
    if (r1.ee_type == EndEffectorType::two_finger) {
      komo.addObjective({1., 1.}, FS_scalarProductXY, {obj, r1_pen_tip}, OT_eq,
                        {1e1}, {0.});
    }

    if (r2.ee_type == EndEffectorType::two_finger) {
      komo.addObjective({2., 2.}, FS_scalarProductXY, {obj, r2_pen_tip}, OT_eq,
                        {1e1}, {0.});
    }
  } else {
    spdlog::info("Trying to grab along y-axis");
    if (r1.ee_type == EndEffectorType::two_finger) {
      komo.addObjective({1., 1.}, FS_scalarProductXX, {obj, r1_pen_tip}, OT_eq,
                        {1e1}, {0.});
    }

    if (r2.ee_type == EndEffectorType::two_finger) {
      komo.addObjective({2., 2.}, FS_scalarProductXX, {obj, r2_pen_tip}, OT_eq,
                        {1e1}, {0.});
    }
  }

  komo.run_prepare(0.0, false);
  const auto inital_state = komo.pathConfig.getJointState();

  const uint max_attempts = 10;
  for (uint j = 0; j < max_attempts; ++j) {
    // reset komo to initial state
    komo.pathConfig.setJointState(inital_state);
    komo.x = inital_state;

    komo.run_prepare(0.0001, false);

    const std::string r1_base_joint_name = get_base_joint_name(r1.type);
    const std::string r2_base_joint_name = get_base_joint_name(r2.type);

    uint r1_cnt = 0;
    uint r2_cnt = 0;
    for (const auto aj : komo.pathConfig.activeJoints) {
      const uint ind = aj->qIndex;
      if (aj->frame->name.contains(r1_base_joint_name.c_str()) &&
          aj->frame->name.contains(r1.prefix.c_str())) {
        // komo.x(ind) = cnt + j;
        if (r1_cnt == 0) {
          // compute orientation for robot to face towards box
          komo.x(ind) = r1_obj_angle + (rnd.uni(-1, 1) * j) / max_attempts;
        }
        if (r1_cnt == 1) {
          // compute orientation for robot to face towards other robot
          komo.x(ind) = r1_r2_angle + (rnd.uni(-1, 1) * j) / max_attempts;
        }
        ++r1_cnt;
      }

      if (aj->frame->name.contains(r2_base_joint_name.c_str()) &&
          aj->frame->name.contains(r2.prefix.c_str())) {
        // komo.x(ind) = cnt + j;
        if (r2_cnt == 1) {
          // compute orientation for robot to face towards box
          komo.x(ind) = r2_r1_angle + (rnd.uni(-1, 1) * j) / max_attempts;
        }
        ++r2_cnt;
      }
    }

    komo.pathConfig.setJointState(komo.x);

    // TODO: replace
    for (const auto f : komo.pathConfig.frames) {
      if (f->name == obj) {
        f->setPose(C[obj]->getPose());
      }
    }
    komo.run_prepare(0.0, false);

    komo.run(options);

    const arr q0 = komo.getPath()[0]();
    const arr q1 = komo.getPath()[1]();

    //   komo.pathConfig.watch(true);

    // ensure via sampling as well
    // std::cout << q0 << std::endl;
    // std::cout << q1 << std::endl;
    // std::cout << q2 << std::endl;

    const auto initial_pose = cp.C.getJointState();

    ConfigurationProblem cp2(C);

    const auto res1 = cp2.query(q0);

    {
      // link object to other robot
      auto from = cp2.C[r1_pen_tip];
      auto to = cp2.C[obj];
      to->unLink();
      // create a new joint
      to->linkFrom(from, true);
    }

    // cp.C.watch(true);
    cp2.C.calc_indexedActiveJoints();

    const auto res2 = cp2.query(q1);
    // cp2.C.watch(true);

    // cp2.C.watch(true);

    cp.C.setJointState(initial_pose);

    const double ineq = komo.getReport(false).get<double>("ineq");
    const double eq = komo.getReport(false).get<double>("eq");

    if (res1->isFeasible && res2->isFeasible && ineq < 1. && eq < 1.) {
      spdlog::debug("found solution for subproblem.");
      // komo.pathConfig.watch(true);
      const auto home = C.getJointState();

      C.setJointState(q0);
      const arr pick_pose = C.getJointState(robot_frames[r1]);

      // std::cout << pick_pose << std::endl;
      // std::cout << q1 << std::endl;
      // std::cout << place_pose << std::endl;

      C.setJointState(home);

      // C.watch(true);

      return {pick_pose, q1};
    } else {
      spdlog::debug("pick/place failed for robot {} and {}, obj {} ineq: "
                    "{:03.2f} eq: {:03.2f}",
                    r1.prefix, r2.prefix, obj, ineq, eq);
      spdlog::debug("Collisions: pose 1 coll: {0}, pose 2 coll: {1}",
                    res1->isFeasible, res2->isFeasible);

      if (!res1->isFeasible) {
        std::stringstream ss;
        res1->writeDetails(ss, cp.C);
        spdlog::debug(ss.str());
      }
      if (!res2->isFeasible) {
        std::stringstream ss;
        res2->writeDetails(ss, cp.C);
        spdlog::debug(ss.str());
      }
      // komo.pathConfig.watch(true);
    }
  }

  return {};
}

class HandoverSampler {
public:
  HandoverSampler(rai::Configuration &_C) : C(_C) {
    delete_unnecessary_frames(C);
    const auto pairs = get_cant_collide_pairs(C);
    C.fcl()->deactivatePairs(pairs);

    // options.allowOverstep = true;
    options.nonStrictSteps = 50;
    options.damping = 10;
    options.wolfe = 0.01;
  };

  rai::Configuration C;
  OptOptions options;

  std::vector<arr>
  sample(const Robot r1, const Robot r2, const rai::String obj,
         const rai::String goal,
         const PickDirection pick_direction_1 = PickDirection::NegZ,
         const PickDirection pick_direction_2 = PickDirection::NegZ,
         const bool sample_pick=true) {
    spdlog::info("computing handover for {0}, {1}, obj {2}", r1.prefix,
                 r2.prefix, obj.p);

    if (!sample_pick){
      spdlog::debug("Not optimizing the pick pose.");
    }

    std::unordered_map<Robot, FrameL> robot_frames;
    for (const auto &r : {r1, r2}) {
      robot_frames[r] = get_robot_joints(C, r);
    }

    setActive(C, std::vector<Robot>{r1, r2});

    const arr obj_pos = C[obj]->getPosition();
    const arr goal_pos = C[goal]->getPosition();

    const arr r1_pos = C[STRING(r1 << "base")]->getPosition();
    const arr r2_pos = C[STRING(r2 << "base")]->getPosition();

    const auto link_to_frame = STRING("table");

    // TODO: solve subproblems to check for feasibility.
    // For now: hardcode the radius of the ur5
    if (euclideanDistance(obj_pos, r1_pos) > 1. ||
        euclideanDistance(goal_pos, r2_pos) > 1. ||
        euclideanDistance(r1_pos, r2_pos) > 1. * 2) {
      spdlog::info("Skipping handover keyframe copmutation for obj {} and "
                   "robots {}, {}",
                   obj.p, r1.prefix, r2.prefix);
      spdlog::info("distance: r1-obj {}, r2-goal {}, r1-r2 {}",
                   euclideanDistance(obj_pos, r1_pos),
                   euclideanDistance(goal_pos, r2_pos),
                   euclideanDistance(r1_pos, r2_pos));
      return {};
    }

    std::vector<arr> subproblem_sol;
    if (sample_pick){
      subproblem_sol = solve_subproblem(C, r1, r2, obj, goal);
    }

    KOMO komo;
    // komo.verbose = 5;
    komo.verbose = 0;
    komo.setModel(C, true);
    // komo.animateOptimization = 5;

    // komo.world.fcl()->deactivatePairs(pairs);
    // komo.pathConfig.fcl()->stopEarly = true;

    komo.setDiscreteOpt(3);

    // komo.add_collision(true, .05, 1e1);
    komo.add_collision(true, .1, 1e1);

    komo.add_jointLimits(true, 0., 1e1);
    komo.addSquaredQuaternionNorms();

    const auto r1_pen_tip = STRING(r1 << r1.ee_frame_name);
    const auto r2_pen_tip = STRING(r2 << r1.ee_frame_name);

    const double r1_z_rot =
        C[STRING(r1 << "base")]->get_X().rot.getEulerRPY()(2);
    const double r2_z_rot =
        C[STRING(r2 << "base")]->get_X().rot.getEulerRPY()(2);

    const double r1_obj_angle =
        std::atan2(obj_pos(1) - r1_pos(1), obj_pos(0) - r1_pos(0)) - r1_z_rot;
    const double r1_r2_angle =
        std::atan2(r2_pos(1) - r1_pos(1), r2_pos(0) - r1_pos(0)) - r1_z_rot;
    const double r2_r1_angle =
        std::atan2(r1_pos(1) - r2_pos(1), r1_pos(0) - r2_pos(0)) - r2_z_rot;
    const double r2_goal_angle =
        std::atan2(goal_pos(1) - r2_pos(1), goal_pos(0) - r2_pos(0)) - r2_z_rot;

    const uint pick_phase = 1;
    const uint handover_phase = 2;
    const uint place_phase = 3;

    Skeleton S;
    S.append({1., 2, SY_stable, {r1_pen_tip, obj}});
    S.append({2., 3., SY_stable, {r2_pen_tip, obj}});
    S.append({3., -1, SY_poseEq, {obj, goal}});

    // Skeleton S = {
    //     // {1., 2., SY_touch, {r1_pen_tip, obj}},
    //     {1., 2, SY_stable, {r1_pen_tip, obj}},
    //     // {2., -1., SY_touch, {r2_pen_tip, obj}},
    //     {2., 3., SY_stable, {r2_pen_tip, obj}},
    //     {3., -1, SY_poseEq, {obj, goal}},
    //     // {3., -1, SY_positionEq, {obj, goal}}
    //     // {3., -1, SY_stable, {obj, goal}},
    // };

    komo.setSkeleton(S);

    const double offset = 0.1;
    komo.addObjective({2., 2.}, FS_distance, {link_to_frame, obj}, OT_ineq,
                      {1e0}, {-offset});

    // komo.addObjective({1., 1.}, FS_aboveBox, {obj, r1_pen_tip}, OT_ineq,
    // {1e2}, {0.0, 0.0, 0.0, 0.0}); komo.addObjective({2., 2.}, FS_aboveBox,
    // {obj, r2_pen_tip}, OT_ineq, {1e2}, {0.1, 0.1, 0.1, 0.1});

    // constraints for picking
    if (sample_pick){
      add_pick_constraints(komo, pick_phase, pick_phase, r1_pen_tip, r1.ee_type,
                          obj, pick_direction_1, C[obj]->shape->size);
    }

    // constraints for the handover
    add_pick_constraints(komo, handover_phase, handover_phase, r2_pen_tip,
                         r2.ee_type, obj, pick_direction_2,
                         C[obj]->shape->size);

    // homing
    if (true) {
      uintA bodies;

      for (const auto &base_name : {r1.prefix, r2.prefix}) {
        rai::Joint *j;
        for (rai::Frame *f : komo.world.frames) {
          if ((j = f->joint) && j->qDim() > 0 &&
              (f->name.contains(base_name.c_str()))) {
            bodies.append(f->ID);
          }
        }
      }

      komo.addObjective({0, 3}, make_shared<F_qItself>(bodies, true), {},
                        OT_sos, {1e-1}, NoArr); // world.q, prec);
    
      if (!sample_pick){
        komo.addObjective({0, 1}, make_shared<F_qItself>(bodies, false), {},
                        OT_eq, {1e1}, komo.world.getJointState());
      }
      
    }

    komo.run_prepare(0.0, false);
    const auto inital_state = komo.pathConfig.getJointState();

    ConfigurationProblem cp(C);

    const uint max_attempts = 10;
    for (uint j = 0; j < max_attempts; ++j) {
      spdlog::debug("Attempting to solve {}th time", j);
      // reset komo to initial state
      komo.pathConfig.setJointState(inital_state);
      komo.x = inital_state;

      komo.run_prepare(0.0001, false);

      const std::string r1_base_joint_name = get_base_joint_name(r1.type);
      const std::string r2_base_joint_name = get_base_joint_name(r2.type);

      uint r1_cnt = 0;
      uint r2_cnt = 0;
      spdlog::debug("initializing bases to face boxes");
      for (const auto aj : komo.pathConfig.activeJoints) {
        const uint ind = aj->qIndex;
        if (aj->frame->name.contains(r1_base_joint_name.c_str()) &&
            aj->frame->name.contains(r1.prefix.c_str())) {
          // komo.x(ind) = cnt + j;
          if (r1_cnt == 0) {
            // compute orientation for robot to face towards box
            komo.x(ind) = r1_obj_angle + (rnd.uni(-1, 1) * j) / max_attempts;
          }
          if (r1_cnt == 1) {
            // compute orientation for robot to face towards other robot
            komo.x(ind) = r1_r2_angle + (rnd.uni(-1, 1) * j) / max_attempts;
          }
          ++r1_cnt;
        }

        if (aj->frame->name.contains(r2_base_joint_name.c_str()) &&
            aj->frame->name.contains(r2.prefix.c_str())) {
          // komo.x(ind) = cnt + j;
          if (r2_cnt == 1) {
            // compute orientation for robot to face towards box
            komo.x(ind) = r2_r1_angle + (rnd.uni(-1, 1) * j) / max_attempts;
          }
          if (r2_cnt == 2) {
            // compute orientation for robot to face towards other robot
            komo.x(ind) = r2_goal_angle + (rnd.uni(-1, 1) * j) / max_attempts;
          }
          ++r2_cnt;
        }
      }

      komo.pathConfig.setJointState(komo.x);

      // initialize stuff
      if (subproblem_sol.size() > 0) {
        spdlog::debug("setting solution from subproblem");
        uintA r1IDs;
        for (const rai::Frame *f : robot_frames[r1]) {
          r1IDs.append(f->ID);
        }
        // std::cout << subproblem_sol[0] << std::endl;
        komo.pathConfig.setJointStateSlice(subproblem_sol[0], 1, r1IDs);

        uintA f2IDs;
        for (const rai::Frame *f : robot_frames[r1]) {
          f2IDs.append(f->ID);
        }
        for (const rai::Frame *f : robot_frames[r2]) {
          f2IDs.append(f->ID);
        }
        // std::cout << subproblem_sol[1] << std::endl;
        komo.pathConfig.setJointStateSlice(subproblem_sol[1], 2, f2IDs);

        // komo.pathConfig.watch(true);
      }

      // initialize object pose to start and goal respectively
      spdlog::debug("Setting object poses");
      uintA objID;
      objID.append(C[obj]->ID);
      rai::Frame *obj1 =
          komo.pathConfig.getFrames(komo.pathConfig.frames.d1 * 1 + objID)(0);
      obj1->setPose(C[obj]->getPose());

      rai::Frame *obj2 =
          komo.pathConfig.getFrames(komo.pathConfig.frames.d1 * 2 + objID)(0);
      obj2->setRelativePosition(obj1->getRelativePosition());
      obj2->setRelativeQuaternion(obj1->getRelativeQuaternion());

      rai::Frame *obj3 =
          komo.pathConfig.getFrames(komo.pathConfig.frames.d1 * 3 + objID)(0);
      obj3->setPose(C[goal]->getPose());

      // komo.pathConfig.watch(true);

      spdlog::debug("Initialized values, running optimizer");
      komo.run_prepare(0.0, false);

      komo.run(options);

      const arr q0 = komo.getPath()[0]();
      const arr q1 = komo.getPath()[1]();
      const arr q2 = komo.getPath()[2]();

      //   komo.pathConfig.watch(true);

      // ensure via sampling as well
      // std::cout << q0 << std::endl;
      // std::cout << q1 << std::endl;
      // std::cout << q2 << std::endl;

      const auto initial_pose = cp.C.getJointState();

      ConfigurationProblem cp2(C);

      const auto res1 = cp2.query(q0);

      {
        // link object to other robot
        auto from = cp2.C[r1_pen_tip];
        auto to = cp2.C[obj];
        to->unLink();
        // create a new joint
        to->linkFrom(from, true);
      }

      // cp.C.watch(true);
      cp2.C.calc_indexedActiveJoints();

      const auto res2 = cp2.query(q1);
      // cp2.C.watch(true);

      {
        auto from = cp2.C[r2_pen_tip];
        auto to = cp2.C[obj];

        to->unLink();

        // create a new joint
        to->linkFrom(from, true);
      }
      cp2.C.calc_indexedActiveJoints();

      const auto res3 = cp2.query(q2);
      // cp2.C.watch(true);

      cp.C.setJointState(initial_pose);

      const double ineq = komo.getReport(false).get<double>("ineq");
      const double eq = komo.getReport(false).get<double>("eq");

      if (res1->isFeasible && res2->isFeasible && res3->isFeasible &&
          ineq < 1. && eq < 1.) {
        // komo.pathConfig.watch(true);
        const auto home = C.getJointState();

        C.setJointState(q0);
        const arr pick_pose = C.getJointState(robot_frames[r1]);

        C.setJointState(q2);
        const arr place_pose = C.getJointState(robot_frames[r2]);

        // std::cout << pick_pose << std::endl;
        // std::cout << q1 << std::endl;
        // std::cout << place_pose << std::endl;

        C.setJointState(home);

        return {pick_pose, q1, place_pose};
      } else {
        spdlog::debug("handover failed for robot {} and {}, obj {} ineq: "
                      "{:03.2f} eq: {:03.2f}",
                      r1.prefix, r2.prefix, obj, ineq, eq);
        spdlog::debug("Collisions: pose 1 coll: {0}, pose 2 coll: {1}, pose "
                      "3 coll: {2}",
                      res1->isFeasible, res2->isFeasible, res3->isFeasible);

        if (!res1->isFeasible) {
          std::stringstream ss;
          res1->writeDetails(ss, cp.C);
          spdlog::debug(ss.str());
        }
        if (!res2->isFeasible) {
          std::stringstream ss;
          res2->writeDetails(ss, cp.C);
          spdlog::debug(ss.str());
        }
        if (!res3->isFeasible) {
          std::stringstream ss;
          res3->writeDetails(ss, cp.C);
          spdlog::debug(ss.str());
        }
        // komo.pathConfig.watch(true);
      }
    }

    return {};
  }
};

std::vector<arr> compute_handover_pose(
    rai::Configuration C, const Robot &r1, const Robot &r2,
    const rai::String &obj, const rai::String &goal,
    const PickDirection pick_direction_1 = PickDirection::NegZ,
    const PickDirection pick_direction_2 = PickDirection::NegZ) {
  HandoverSampler sampler(C);
  return sampler.sample(r1, r2, obj, goal, pick_direction_1, pick_direction_2);
}

RobotTaskPoseMap
compute_all_handover_poses(rai::Configuration C,
                           const std::vector<Robot> &robots,
                           const bool attempt_all_directions = false) {
  uint num_objects = 0;
  for (auto f : C.frames) {
    if (f->name.contains("obj")) {
      num_objects += 1;
    }
  }

  std::vector<std::pair<PickDirection, PickDirection>> directions;
  if (attempt_all_directions) {
    for (int i = 5; i >= 0; --i) {
      for (int j = 5; j >= 0; --j) {
        directions.push_back(
            std::make_pair(PickDirection(i), PickDirection(j)));
      }
    }
  } else {
    directions = {std::make_pair(PickDirection::NegZ, PickDirection::NegZ)};
  }

  delete_unnecessary_frames(C);
  const auto pairs = get_cant_collide_pairs(C);
  C.fcl()->deactivatePairs(pairs);

  HandoverSampler sampler(C);
  RobotTaskPoseMap rtpm;

  // check if we are currently holding an object with the robot that we are computing the keyframe for
  std::vector<std::pair<Robot, rai::String>> held_objs;
  for (const Robot &r : robots) {
    for (const auto &c: sampler.C[STRING(r.prefix + "pen_tip")]->children){
      // std::cout << c->name << std::endl;
      if (c->name.contains("obj")){
        held_objs.push_back(std::make_pair(r, c->name));
      }
    }
  }

  for (const auto &r1 : robots) {
    for (const auto &r2 : robots) {
      for (const auto &robot_obj_pair: held_objs){
        if (r1 == robot_obj_pair.first || r2 == robot_obj_pair.first){
          // if we are planning keyframes for this robot, and the robot is holding something, 
          // we need to disable the collision for this object
          sampler.C[robot_obj_pair.second]->setContact(0);
          break;
        }
      }

      if (r1 == r2) {
        continue;
      }

      setActive(C, std::vector<Robot>{r1, r2});

      for (uint i = 0; i < num_objects; ++i) {
        const auto obj = STRING("obj" << i + 1);
        const auto goal = STRING("goal" << i + 1);

        bool is_held_by_other_robot = false;
        bool is_held_by_this_robot = false;
        for (const auto &robot_obj_pair: held_objs){
          if (robot_obj_pair.second == obj && (robot_obj_pair.first != r1)){
            is_held_by_other_robot = true;
            break;
          }
          if (robot_obj_pair.second == obj && robot_obj_pair.first == r1){
            is_held_by_this_robot = true;
            break;
          }

        }

        if (is_held_by_other_robot){
          continue;
        }

        spdlog::info("computing handover for {0}, {1}, obj {2}", r1.prefix,
                     r2.prefix, i + 1);

        const auto obj_quat = C[obj]->getRelativeQuaternion();

        std::vector<std::pair<PickDirection, PickDirection>> reordered_directions;
        for (const auto &d: directions){
          if (euclideanDistance(dir_to_vec(d.first), -get_pos_z_axis_dir(obj_quat)) < 1e-6 || 
              euclideanDistance(dir_to_vec(d.second), -get_pos_z_axis_dir(obj_quat)) < 1e-6 ){
            reordered_directions.push_back(d);
          }
        }

        for (const auto &dir: directions){
          if (std::find(reordered_directions.begin(), reordered_directions.end(), dir) == reordered_directions.end()){
            reordered_directions.push_back(dir);
          }
        }

        for (const auto &dirs : reordered_directions) {

          const auto sol =
              sampler.sample(r1, r2, obj, goal, dirs.first, dirs.second, !is_held_by_this_robot);

          // const auto sol = compute_handover_pose(C, r1, r2, obj, goal);

          if (sol.size() > 0) {
            RobotTaskPair rtp;
            rtp.robots = {r1, r2};
            rtp.task = Task{.object = i, .type = PrimitiveType::handover};

            rtpm[rtp].push_back(sol);
            break;
          }

          else {
            spdlog::info("Could not find a solution.");
          }
        }
      
      }

      for (const auto &robot_obj_pair: held_objs){
        if (r1 == robot_obj_pair.first || r2 == robot_obj_pair.first){
          // if we are planning keyframes for this robot, and the robot is holding something, 
          // we need to disable the collision for this object
          sampler.C[robot_obj_pair.second]->setContact(1);
          break;
        }
      }
    }
  }

  return rtpm;
}