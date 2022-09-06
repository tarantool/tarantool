import yaml
import pandas as pd

results_file_name = "results.yml"
builds_key = "builds"
build_key = "build"
build_name_key = "build_name"
tuple_count_key = "tuple_count"
spaces_key = "spaces"
space_key = "space"
space_name_key = "space_name"
insert_key = "insert"
select_key = "select"
bloom_size_key = "bloom_size"


def print_result(result):
    pd.set_option("display.max_columns", None)
    pd.set_option("display.precision", 0)
    print(result)


def get_build_info(build):
    build_name = build[build_name_key]
    tuple_count = build[tuple_count_key]
    spaces = build[spaces_key]
    space_infos = []
    for space_item in spaces:
        space = space_item[space_key]
        space_name = space[space_name_key]
        insert = space[insert_key]
        select = space[select_key]
        bloom_size = space[bloom_size_key]
        space_infos.append([space_name, insert, select, bloom_size])
    return [build_name, tuple_count, space_infos]


if __name__ == "__main__":
    with open(results_file_name, "r") as stream:
        try:
            data = yaml.safe_load(stream)
            builds = data[builds_key]
            frames = []
            for build_item in builds:
                build = build_item[build_key]
                frame = pd.json_normalize(build, spaces_key, [build_name_key, tuple_count_key])
                frame.columns = [space_name_key, insert_key, select_key, bloom_size_key,
                                 build_name_key, tuple_count_key]
                frames.append(frame)
            concatenated = pd.concat(frames)
            concatenated.to_csv("results.csv")
            mean = concatenated.groupby([tuple_count_key, build_name_key, space_name_key]).mean()
            mean.to_csv("mean.csv")
            print_result(mean)
        except yaml.YAMLError as exc:
            print(exc)
