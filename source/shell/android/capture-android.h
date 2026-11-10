#pragma once
#include "ui/audio_capture.h"
#include "android/android-jni.h"
namespace ptshell {
class AndroidCapture final:public pt::ui::CaptureBackend {
public:
 std::string inputs_json()override{return query("captureInputsJson");}
 std::string state_json()override{return query("captureStateJson");}
 bool start(int id,int channels,int rate)override{
  Attached a;if(!a.ok())return false;auto m=a.method("captureStart","(III)Z");
  if(!m)return false;const auto result=a.env->CallBooleanMethod(a.activity,m,id,channels,rate);
  return !a.failed()&&result==JNI_TRUE;
 }
 void stop()override{Attached a;if(!a.ok())return;auto m=a.method("captureStop","()V");if(m){a.env->CallVoidMethod(a.activity,m);a.failed();}}
private:
 static std::string query(const char* name){Attached a;if(!a.ok())return {};auto m=a.method(name,"()Ljava/lang/String;");if(!m)return {};auto obj=a.env->CallObjectMethod(a.activity,m);if(a.failed())return {};return a.take(obj);}
};
}
