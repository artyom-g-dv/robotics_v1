//Corresponding header
#include "robo_common/layout/animators/GameEndAnimator.h"

//C system headers

//C++ system headers

//Other libraries headers
#include "utils/data_type/EnumClassUtils.h"
#include "utils/ErrorCode.h"
#include "utils/Log.h"

//Own components headers

int32_t GameEndAnimator::init(const ShutdownGameCb& shutdownGameCb) {
  if (nullptr == shutdownGameCb) {
    LOGERR("Error, nullptr provided for ShutdownGameCb");
    return FAILURE;
  }
  _shutdownGameCb = shutdownGameCb;

  return SUCCESS;
}

void GameEndAnimator::draw() const {

}

void GameEndAnimator::startGameWonAnim() {
  LOGG("You've Won");

  //TODO add animation and invoke shutdownGameCb on animation end
  _shutdownGameCb();
}

void GameEndAnimator::startGameLostAnim() {
  LOGR("You've Lost");

  //TODO add animation and invoke shutdownGameCb on animation end
  _shutdownGameCb();
}

void GameEndAnimator::startAchievementWonAnim(Achievement achievement) {
  switch (achievement) {
  case Achievement::SINGLE_STAR:
    LOGG("You've Won Single Star");
    break;
  case Achievement::DOUBLE_STAR:
    LOGG("You've Won Double Star");
    break;
  case Achievement::TRIPLE_STAR:
    LOGG("You've Won Tripple Star");
    break;
  default:
    LOGERR("Error, received unsupported Achievement value: %d",
        getEnumValue(achievement));
    break;
  }
}

