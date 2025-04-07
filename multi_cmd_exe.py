import os
from makespan_utils import *
import random
from datetime import datetime

def get_cmd_str_to_generate_sequences(relative_path_to_robot_file,
                                      relative_path_to_obj_file,
                                      output_path,
                                      r_seed):
    cmd_str = "echo $PWD && cd /home/tapas/multi-agent-tamp-solver/24-data-gen/ && echo $PWD && "
    cmd_str += "xvfb-run -a --server-args=\"-screen 0 480x480x24\" "
    cmd_str += "./x.exe -pnp true -mode generate_candidate_sequences -seed " + str(r_seed) + " "
    cmd_str += "-attempt_all_grasp_directions true "
    cmd_str += "-robot_path " + relative_path_to_robot_file + " "
    cmd_str += "-obj_path " + relative_path_to_obj_file + " "
    cmd_str += "--attempt_komo false -display false -export_images false -verbosity 5 -early_stopping false "
    cmd_str += "-scene_path 'in/scenes/floor.g' "
    # cmd_str += "-obstacle_path 'in/obstacles/shelf.json' "
    cmd_str += "-output_path " + output_path + " "
    return cmd_str

def get_cmd_str_to_plan_for_sequence(relative_path_to_robot_file,
                                     relative_path_to_obj_file,
                                     relative_path_to_seq_file,
                                     output_path,
                                     r_seed):
    cmd_str = "echo $PWD && cd /home/tapas/multi-agent-tamp-solver/24-data-gen/ && echo $PWD && "
    cmd_str += "xvfb-run -a --server-args=\"-screen 0 480x480x24\" "
    cmd_str += "./x.exe -pnp true -mode plan_for_sequence -seed " + str(r_seed) + " "
    cmd_str += "-attempt_all_grasp_directions true "
    cmd_str += "-robot_path " + relative_path_to_robot_file + " "
    cmd_str += "-obj_path " + relative_path_to_obj_file + " "
    cmd_str += "--attempt_komo false -display false -export_images false -verbosity 5 -early_stopping false "
    cmd_str += "-scene_path 'in/scenes/floor.g' "
    # cmd_str += "-obstacle_path 'in/obstacles/shelf.json' "
    cmd_str += "-sequence_path " + relative_path_to_seq_file + " "
    cmd_str += "-output_path " + output_path + " "
    return cmd_str

def latest_sequences(folder_path):
    subfolders = [f.path for f in os.scandir(folder_path) if f.is_dir()]
    sequences = [s for s in subfolders if s.startswith(f"{output_path}sequence_generation")]  # the list of all generated sequences
    if not sequences:
        return None
    
    latest_sequences = max(sequences, key=os.path.getmtime)

    return latest_sequences


ROB_NAME = "conveyor_robot_output_5_rela"
OBJ_NAME = "conveyor_obj_output_5_rela"

solver_path = "/home/tapas/multi-agent-tamp-solver/24-data-gen/"

path_to_input_files = "replan_data/replan_ini_conveyor_5_20250225_201416/"
with open(f"{path_to_input_files}part_hold_info_conveyor_5.json", "r") as file:
    part_hold_info = json.load(file)

for i in range(len(part_hold_info)):
    subdir = part_hold_info[i]["hold_step"]
    path_to_robot_file =  path_to_input_files + f"{subdir}"+"/" + ROB_NAME + ".json" 
    path_to_obj_file =  path_to_input_files + f"{subdir}"+"/" + OBJ_NAME + ".json"
    output_path = path_to_input_files + f"{subdir}" + "/"
    print("path ", path_to_input_files)
    # if not os.path.exists(full_path_to_input_files):
    #     os.makedirs(full_path_to_input_files)

    r_seed = random.randint(0, 9999)
    # Generate sequences
    cmd_str = get_cmd_str_to_generate_sequences(path_to_robot_file,
                                                path_to_obj_file,
                                                output_path,
                                                r_seed)
    exec_cmd(cmd_str)

    # plan for sequences
    seq_name = "sequence_generation"
    latest_sequence = latest_sequences(path_to_input_files) # return the absolute path value
    seq_fullname = latest_sequence.removeprefix(f"{path_to_input_files}")
    print(seq_fullname)
    relative_path_to_seq_file = seq_fullname + "/" + "sequences.json"
    cmd_str = get_cmd_str_to_plan_for_sequence(path_to_robot_file, 
                                                        path_to_obj_file, 
                                                        relative_path_to_seq_file, 
                                                        output_path,
                                                        r_seed)
    exec_cmd(cmd_str)
