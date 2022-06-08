//Corresponding header
#include "robo_cleaner_gui/external_api/CleanerControllerExternalBridge.h"

//System headers

//Other libraries headers
#include "robo_cleaner_common/defines/RoboCleanerTopics.h"
#include "robo_cleaner_common/message_helpers/RoboCleanerMessageHelpers.h"
#include "robo_common/defines/RoboCommonDefines.h"
#include "utils/data_type/EnumClassUtils.h"
#include "utils/ErrorCode.h"
#include "utils/Log.h"

//Own components headers
#include "robo_cleaner_gui/helpers/RoboCleanerSolutionValidator.h"
#include "robo_cleaner_gui/helpers/EnergyHandler.h"

using namespace std::placeholders;

CleanerControllerExternalBridge::CleanerControllerExternalBridge()
    : Node("CleanerControllerExternalBridge") {

}

ErrorCode CleanerControllerExternalBridge::init(
    const CleanerControllerExternalBridgeOutInterface &interface) {
  if (ErrorCode::SUCCESS != initOutInterface(interface)) {
    LOGERR("Error, initOutInterface() failed");
    return ErrorCode::FAILURE;
  }

  if (ErrorCode::SUCCESS != initCommunication()) {
    LOGERR("Error, initCommunication() failed");
    return ErrorCode::FAILURE;
  }

  return ErrorCode::SUCCESS;
}

void CleanerControllerExternalBridge::publishShutdownController() {
  _shutdownControllerPublisher->publish(Empty());

  const auto f = [this]() {
    _outInterface.systemShutdownCb();
  };
  _outInterface.invokeActionEventCb(f, ActionEventType::NON_BLOCKING);
}

void CleanerControllerExternalBridge::publishFieldMapRevealed() {
  const auto f = [this]() {
    _outInterface.solutionValidator->fieldMapRevealed();
  };
  _outInterface.invokeActionEventCb(f, ActionEventType::NON_BLOCKING);

  _fieldMapReveleadedPublisher->publish(Empty());
}

void CleanerControllerExternalBridge::publishFieldMapCleaned() {
  const auto f = [this]() {
    _outInterface.solutionValidator->fieldMapCleaned();
  };
  _outInterface.invokeActionEventCb(f, ActionEventType::NON_BLOCKING);

  _fieldMapCleanedPublisher->publish(Empty());
}

void CleanerControllerExternalBridge::resetControllerStatus() {
  const auto f = [this]() {
    _controllerStatus = ControllerStatus::IDLE;
  };
  _outInterface.invokeActionEventCb(f, ActionEventType::NON_BLOCKING);
}

ErrorCode CleanerControllerExternalBridge::initOutInterface(
    const CleanerControllerExternalBridgeOutInterface &outInterface) {
  _outInterface = outInterface;
  if (nullptr == _outInterface.invokeActionEventCb) {
    LOGERR("Error, nullptr provided for InvokeActionEventCb");
    return ErrorCode::FAILURE;
  }

  if (!_outInterface.robotActInterface.isValid()) {
    LOGERR("Error, RobotActInterface is not populated");
    return ErrorCode::FAILURE;
  }

  if (nullptr == _outInterface.systemShutdownCb) {
    LOGERR("Error, nullptr provided for SystemShutdownCb");
    return ErrorCode::FAILURE;
  }

  if (nullptr == _outInterface.startGameLostAnimCb) {
    LOGERR("Error, nullptr provided for StartGameLostAnimCb");
    return ErrorCode::FAILURE;
  }

  if (nullptr == _outInterface.startGameWonAnimCb) {
    LOGERR("Error, nullptr provided for startGameWonAnimCb");
    return ErrorCode::FAILURE;
  }

  if (nullptr == _outInterface.acceptGoalCb) {
    LOGERR("Error, nullptr provided for AcceptGoalCb");
    return ErrorCode::FAILURE;
  }

  if (nullptr == _outInterface.reportRobotStartingActCb) {
    LOGERR("Error, nullptr provided for ReportRobotStartingActCb");
    return ErrorCode::FAILURE;
  }

  if (nullptr == _outInterface.reportInsufficientEnergyCb) {
    LOGERR("Error, nullptr provided for ReportInsufficientEnergyCb");
    return ErrorCode::FAILURE;
  }

  if (nullptr == _outInterface.cancelFeedbackReportingCb) {
    LOGERR("Error, nullptr provided for CancelFeedbackReportingCb");
    return ErrorCode::FAILURE;
  }

  if (nullptr == _outInterface.energyHandler) {
    LOGERR("Error, nullptr provided for energyHandler");
    return ErrorCode::FAILURE;
  }

  if (nullptr == _outInterface.solutionValidator) {
    LOGERR("Error, nullptr provided for SolutionValidator");
    return ErrorCode::FAILURE;
  }

  return ErrorCode::SUCCESS;
}

ErrorCode CleanerControllerExternalBridge::initCommunication() {
  constexpr auto queueSize = 10;
  _shutdownControllerPublisher = create_publisher<Empty>(
      SHUTDOWN_CONTROLLER_TOPIC, queueSize);

  _fieldMapReveleadedPublisher = create_publisher<Empty>(
      FIELD_MAP_REVEALED_TOPIC, queueSize);

  _fieldMapCleanedPublisher = create_publisher<Empty>(FIELD_MAP_CLEANED_TOPIC,
      queueSize);

  _batteryStatusService = create_service<QueryBatteryStatus>(
      QUERY_BATTERY_STATUS_SERVICE,
      std::bind(&CleanerControllerExternalBridge::handleBatteryStatusService,
          this, _1, _2));

  _initialRobotStateService = create_service<QueryInitialRobotState>(
      QUERY_INITIAL_ROBOT_STATE_SERVICE,
      std::bind(
          &CleanerControllerExternalBridge::handleInitialRobotStateService,
          this, _1, _2));

  _moveActionServer = rclcpp_action::create_server<RobotMove>(this,
      ROBOT_MOVE_ACTION,
      std::bind(&CleanerControllerExternalBridge::handleMoveGoal, this, _1, _2),
      std::bind(&CleanerControllerExternalBridge::handleMoveCancel, this, _1),
      std::bind(&CleanerControllerExternalBridge::handleMoveAccepted, this,
          _1));

  return ErrorCode::SUCCESS;
}

rclcpp_action::GoalResponse CleanerControllerExternalBridge::handleMoveGoal(
    const rclcpp_action::GoalUUID &uuid,
    std::shared_ptr<const RobotMove::Goal> goal) {
  LOG("Received goal request with moveType: %hhd and uuid: %s",
      goal->robot_move_type.move_type, rclcpp_action::to_string(uuid).c_str());

  const auto moveType = getMoveType(goal->robot_move_type.move_type);
  if (MoveType::UNKNOWN == moveType) {
    LOGERR(
        "Error, Rejecting goal with uuid: %s because of unsupported " "MoveType",
        rclcpp_action::to_string(uuid).c_str());
    return rclcpp_action::GoalResponse::REJECT;
  }

  auto response = rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  const auto f =
      [this, &uuid, &response]() {
        if (ControllerStatus::ACTIVE == _controllerStatus) {
          LOGERR(
              "Error, Rejecting goal with uuid: %s because another one is " "already active",
              rclcpp_action::to_string(uuid).c_str());
          response = rclcpp_action::GoalResponse::REJECT;
          return;
        }

        _controllerStatus = ControllerStatus::ACTIVE;
      };
  _outInterface.invokeActionEventCb(f, ActionEventType::BLOCKING);

  return response;
}

rclcpp_action::CancelResponse CleanerControllerExternalBridge::handleMoveCancel(
    const std::shared_ptr<GoalHandleRobotMove> goalHandle) {
  LOG(
      "Received request to cancel goal with uuid: %s, Rolling back robot " "position/rotation to previous state",
      rclcpp_action::to_string(goalHandle->get_goal_id()).c_str());

  const auto f = [this]() {
    //First cancel the robot more to initiate state rollback
    _outInterface.robotActInterface.cancelRobotMove();

    //then cancel feedback reporting and process the goal handle canceling state
    _outInterface.cancelFeedbackReportingCb();
  };
  _outInterface.invokeActionEventCb(f, ActionEventType::NON_BLOCKING);

  return rclcpp_action::CancelResponse::ACCEPT;
}

void CleanerControllerExternalBridge::handleMoveAccepted(
    const std::shared_ptr<GoalHandleRobotMove> goalHandle) {
  const auto goal = goalHandle->get_goal();
  const MoveType moveType = getMoveType(goal->robot_move_type.move_type);
  const auto f = [this, moveType]() {
    _outInterface.solutionValidator->increaseTotalRobotMovesCounter(1);

    const auto [success, penaltyTurns] =
        _outInterface.energyHandler->initiateMove();
    if (!success) {
      _outInterface.energyHandler->performPenaltyChange();
      _outInterface.reportInsufficientEnergyCb(penaltyTurns);
      return;
    }

    _outInterface.robotActInterface.actCb(moveType);
    const char approachMarker =
        _outInterface.solutionValidator->getApproachingTileMarker(moveType);
    _outInterface.reportRobotStartingActCb(moveType, approachMarker);
  };
  _outInterface.invokeActionEventCb(f, ActionEventType::NON_BLOCKING);

  // return quickly to avoid blocking the executor
  _outInterface.acceptGoalCb(goalHandle);
}

void CleanerControllerExternalBridge::handleBatteryStatusService(
    [[maybe_unused]]const std::shared_ptr<QueryBatteryStatus::Request> request,
    std::shared_ptr<QueryBatteryStatus::Response> response) {
  const auto f = [this, &response]() {
    const auto [maxMoves, movesLeft] =
        _outInterface.energyHandler->queryBatteryStatus();
    response->battery_status.max_moves_on_full_energy = maxMoves;
    response->battery_status.moves_left = movesLeft;
  };

  _outInterface.invokeActionEventCb(f, ActionEventType::BLOCKING);
}

void CleanerControllerExternalBridge::handleInitialRobotStateService(
    [[maybe_unused]]const std::shared_ptr<QueryInitialRobotState::Request> request,
    std::shared_ptr<QueryInitialRobotState::Response> response) {
  const auto f = [this, &response]() {
    InitialRobotState initialRobotState;
    const auto [success, majorError] =
        _outInterface.solutionValidator->queryInitialRobotPos(initialRobotState,
            response->error_reason);
    response->success = success && !majorError;
    if (majorError) {
      _outInterface.startGameLostAnimCb();
      return;
    }

    response->initial_robot_state.robot_dir =
        getRobotDirectionField(initialRobotState.robotDir);
    response->initial_robot_state.robot_tile = initialRobotState.robotTile;

    const auto [maxMoves, movesLeft] =
        _outInterface.energyHandler->queryBatteryStatus();
    response->initial_robot_state.battery_status.max_moves_on_full_energy =
        maxMoves;
    response->initial_robot_state.battery_status.moves_left = movesLeft;
  };

  _outInterface.invokeActionEventCb(f, ActionEventType::BLOCKING);
}

