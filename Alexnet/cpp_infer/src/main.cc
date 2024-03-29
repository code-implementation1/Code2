/**
 * Copyright 2021 Huawei Technologies Co., Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <sys/time.h>

#include <dirent.h>
#include <iostream>
#include <string>
#include <cstring>
#include <algorithm>
#include <iosfwd>
#include <vector>
#include <fstream>
#include <sstream>

#include "common_inc/infer.h"

DEFINE_string(mindir_path, "", "mindir path");
DEFINE_string(dataset_name, "cifar10", "['cifar10', 'imagenet2012']");
DEFINE_string(input0_path, ".", "input0 path");
DEFINE_int32(device_id, 0, "device id");
DEFINE_string(device_type, "CPU", "device type");

int predict_cifar10(std::string input_path, std::vector<MSTensor> &model_inputs, Model *model,
                    std::map<double, double> &costTime_map) {
  auto input0_files = GetAllFiles(input_path);
  if (input0_files.empty()) {
    std::cout << "ERROR: no input data." << std::endl;
    return 1;
  }
  std::vector<MSTensor> inputs = model->GetInputs();
  for (size_t i = 0; i < input0_files.size(); ++i) {
    struct timeval start = {0};
    struct timeval end = {0};
    double startTimeMs;
    double endTimeMs;
    std::vector<MSTensor> outputs;
    std::cout << "Start predict input files:" << input0_files[i] << std::endl;
    auto input0 = ReadFileToTensor(input0_files[i]);
    if (inputs[0].DataSize() < input0.DataSize()) {
      std::cout << "ERROR: input data sizes do not match" << std::endl;
      return 1;
    }
    std::memcpy(inputs[0].MutableData(), input0.Data().get(), input0.DataSize());

    gettimeofday(&start, nullptr);
    Status ret = model->Predict(inputs, &outputs);
    gettimeofday(&end, nullptr);
    if (ret != kSuccess) {
      std::cout << "Predict " << input0_files[i] << " failed." << std::endl;
      return 1;
    }
    startTimeMs = (1.0 * start.tv_sec * 1000000 + start.tv_usec) / 1000;
    endTimeMs = (1.0 * end.tv_sec * 1000000 + end.tv_usec) / 1000;
    costTime_map.insert(std::pair<double, double>(startTimeMs, endTimeMs));
    int rst = WriteResult(input0_files[i], outputs);
    if (rst != 0) {
      std::cout << "write result failed." << std::endl;
      return rst;
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  if (!ParseCommandLineFlags(argc, argv)) {
    std::cout << "Failed to parse args" << std::endl;
    return 1;
  }
  if (RealPath(FLAGS_mindir_path).empty()) {
    std::cout << "Invalid mindir" << std::endl;
    return 1;
  }
  Model model;
  if (!LoadModel(FLAGS_mindir_path, FLAGS_device_type, FLAGS_device_id, &model)) {
    std::cout << "Failed to load model " << FLAGS_mindir_path << ", device id: " << FLAGS_device_id
              << ", device type: " << FLAGS_device_type;
    return 1;
  }
  auto model_inputs = model.GetInputs();
  if (model_inputs.empty()) {
    std::cout << "Invalid model, inputs is empty." << std::endl;
    return 1;
  }

  std::map<double, double> costTime_map;

  if (FLAGS_dataset_name == "cifar10") {
    int ret = predict_cifar10(FLAGS_input0_path, model_inputs, &model, costTime_map);
    if (ret != 0) {
      return ret;
    }
  } else {
    auto input0_files = GetAllInputData(FLAGS_input0_path);
    if (input0_files.empty()) {
      std::cout << "ERROR: no input data." << std::endl;
      return 1;
    }
    for (size_t i = 0; i < input0_files.size(); ++i) {
      for (size_t j = 0; j < input0_files[i].size(); ++j) {
        struct timeval start = {0};
        struct timeval end = {0};
        double startTimeMs;
        double endTimeMs;
        std::vector<MSTensor> inputs;
        std::vector<MSTensor> outputs;
        std::cout << "Start predict input files:" << input0_files[i][j] << std::endl;
        auto decode = Decode();
        auto resize = Resize({256, 256});
        auto centercrop = CenterCrop({224, 224});

        Execute SingleOp({decode, resize, centercrop});
        auto imgDvpp = std::make_shared<MSTensor>();
        SingleOp(ReadFileToTensor(input0_files[i][j]), imgDvpp.get());
        inputs.emplace_back(model_inputs[0].Name(), model_inputs[0].DataType(), model_inputs[0].Shape(),
                            imgDvpp->Data().get(), imgDvpp->DataSize());
        gettimeofday(&start, nullptr);
        Status ret = model.Predict(inputs, &outputs);
        gettimeofday(&end, nullptr);
        if (ret != kSuccess) {
          std::cout << "Predict " << input0_files[i][j] << " failed." << std::endl;
          return 1;
        }
        startTimeMs = (1.0 * start.tv_sec * 1000000 + start.tv_usec) / 1000;
        endTimeMs = (1.0 * end.tv_sec * 1000000 + end.tv_usec) / 1000;
        costTime_map.insert(std::pair<double, double>(startTimeMs, endTimeMs));
        int rst = WriteResult(input0_files[i][j], outputs);
        if (rst != 0) {
          std::cout << "write result failed." << std::endl;
          return rst;
        }
      }
    }
  }
  double average = 0.0;
  int inferCount = 0;

  for (auto iter = costTime_map.begin(); iter != costTime_map.end(); iter++) {
    average += (iter->second - iter->first);
    inferCount++;
  }
  average = average / inferCount;
  std::stringstream timeCost;
  timeCost << "NN inference cost average time: " << average << " ms of infer_count " << inferCount << std::endl;
  std::cout << "NN inference cost average time: " << average << "ms of infer_count " << inferCount << std::endl;
  std::string fileName = "./time_Result" + std::string("/test_perform_static.txt");
  std::ofstream fileStream(fileName.c_str(), std::ios::trunc);
  fileStream << timeCost.str();
  fileStream.close();
  costTime_map.clear();
  return 0;
}
