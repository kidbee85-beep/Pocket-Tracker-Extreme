#pragma once
#include "ui/source_sampler.h"
#include "android/android-jni.h"
namespace ptshell {
class AndroidSource final:public pt::ui::SourceBackend {
public:
 bool command(const std::string& action,const std::string& argsJson)override{
  Attached a;if(!a.ok())return false;
  auto m=a.method("sourceCommand","(Ljava/lang/String;Ljava/lang/String;)Z");if(!m)return false;
  auto actionJ=a.env->NewStringUTF(action.c_str());auto argsJ=a.env->NewStringUTF(argsJson.c_str());
  if(!actionJ||!argsJ){if(actionJ)a.env->DeleteLocalRef(actionJ);if(argsJ)a.env->DeleteLocalRef(argsJ);a.failed();return false;}
  const auto result=a.env->CallBooleanMethod(a.activity,m,actionJ,argsJ);
  a.env->DeleteLocalRef(actionJ);a.env->DeleteLocalRef(argsJ);
  return !a.failed()&&result==JNI_TRUE;
 }
 std::string state_json()override{
  Attached a;if(!a.ok())return {};auto m=a.method("sourceStateJson","()Ljava/lang/String;");if(!m)return {};
  auto obj=a.env->CallObjectMethod(a.activity,m);if(a.failed()||!obj)return {};
  // JSON carries public titles, including supplementary Unicode characters. JNI's
  // modified UTF-8 would encode them as surrogate pairs, which a JSON parser rejects.
  auto cls=a.env->GetObjectClass(obj);auto getBytes=a.env->GetMethodID(cls,"getBytes","(Ljava/lang/String;)[B");
  auto charset=a.env->NewStringUTF("UTF-8");
  auto bytes=getBytes?static_cast<jbyteArray>(a.env->CallObjectMethod(obj,getBytes,charset)):nullptr;
  a.env->DeleteLocalRef(charset);a.env->DeleteLocalRef(cls);a.env->DeleteLocalRef(obj);
  if(a.failed()||!bytes)return {};
  const auto length=a.env->GetArrayLength(bytes);std::string out(static_cast<size_t>(length),'\0');
  if(length)a.env->GetByteArrayRegion(bytes,0,length,reinterpret_cast<jbyte*>(&out[0]));
  a.env->DeleteLocalRef(bytes);return a.failed()?std::string():out;
 }
};
}
