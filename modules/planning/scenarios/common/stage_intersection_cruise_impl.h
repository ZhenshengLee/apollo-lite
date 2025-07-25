/******************************************************************************
 * Copyright 2019 The Apollo Authors. All Rights Reserved.
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
 *****************************************************************************/

/**
 * @file
 **/

#pragma once

#include "modules/planning/proto/planning_config.pb.h"

#include "modules/planning/common/frame.h"

namespace apollo {
namespace planning {
namespace scenario {

class StageIntersectionCruiseImpl {
 public:
  bool CheckDone(const Frame& frame, const ScenarioType& scenario_type,
                 const ScenarioConfig::StageConfig& config,
                 const PlanningContext* context,
                 const bool right_of_way_status);
};

}  // namespace scenario
}  // namespace planning
}  // namespace apollo
