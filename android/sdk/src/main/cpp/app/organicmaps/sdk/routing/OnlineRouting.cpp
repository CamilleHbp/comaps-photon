#include "app/organicmaps/sdk/routing/OnlineRouting.hpp"
#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"
#include "routing/valhalla_router.hpp"
#include "routing/router_delegate.hpp"
#include <chrono>

void InstallOnlineCarTransport(JNIEnv * env)
{
  // Resolve application classes on the Java caller thread, not the native routing worker.
  static jclass const transport = jni::GetGlobalClassRef(env, "app/organicmaps/sdk/routing/ValhallaTransport");
  static jclass const requestClass = jni::GetGlobalClassRef(env, "app/organicmaps/sdk/routing/ValhallaTransport$Request");
  static jmethodID const start = env->GetStaticMethodID(transport, "start",
      "(Ljava/lang/String;Ljava/lang/String;)Lapp/organicmaps/sdk/routing/ValhallaTransport$Request;");
  static jmethodID const poll = env->GetMethodID(requestClass, "await", "(I)I");
  static jmethodID const cancel = env->GetMethodID(requestClass, "cancel", "()V");
  static jmethodID const body = env->GetMethodID(requestClass, "getBody", "()Ljava/lang/String;");
  g_framework->GetRoutingManager().SetCarRoutingTransport([](std::string const & url, std::string const & payload,
                                                           routing::RouterDelegate const & delegate)
  {
    using namespace routing;
    auto * worker = jni::GetEnv();
    jni::TScopedLocalRef endpoint(worker, jni::ToJavaString(worker, url));
    jni::TScopedLocalRef data(worker, jni::ToJavaString(worker, payload));
    jni::TScopedLocalRef request(worker, worker->CallStaticObjectMethod(transport, start, endpoint.get(), data.get()));
    if (worker->ExceptionCheck()) { worker->ExceptionClear(); return OnlineResponse{}; }
    if (!request.get()) return OnlineResponse{};
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (;;)
    {
      bool const cancelled = delegate.IsCancelled();
      if (cancelled || std::chrono::steady_clock::now() >= deadline)
      {
        worker->CallVoidMethod(request.get(), cancel);
        if (worker->ExceptionCheck()) worker->ExceptionClear();
        return OnlineResponse{cancelled ? OnlineResult::Cancelled : OnlineResult::Timeout, {}};
      }
      auto const status = worker->CallIntMethod(request.get(), poll, 25);
      if (worker->ExceptionCheck())
      {
        worker->ExceptionClear(); worker->CallVoidMethod(request.get(), cancel);
        if (worker->ExceptionCheck()) worker->ExceptionClear();
        return OnlineResponse{};
      }
      if (status == 0) continue;
      if (status == 1)
      {
        jni::ScopedLocalRef<jstring> text(worker, static_cast<jstring>(worker->CallObjectMethod(request.get(), body)));
        if (worker->ExceptionCheck()) { worker->ExceptionClear(); return OnlineResponse{}; }
        return OnlineResponse{OnlineResult::Ok, jni::ToNativeString(worker, text.get())};
      }
      return OnlineResponse{status == 2 ? OnlineResult::Cancelled : status == 3 ? OnlineResult::Timeout :
                            status == 4 ? OnlineResult::Busy : OnlineResult::Unavailable, {}};
    }
  });
}

extern "C"
{
JNIEXPORT jboolean JNICALL Java_app_organicmaps_sdk_routing_OnlineRouting_nativeIsEnabled(JNIEnv *, jclass)
{ return RoutingManager::GetCarRoutingConfig().online; }
JNIEXPORT jstring JNICALL Java_app_organicmaps_sdk_routing_OnlineRouting_nativeGetEndpoint(JNIEnv * env, jclass)
{ return jni::ToJavaString(env, RoutingManager::GetCarRoutingConfig().endpoint); }
JNIEXPORT jboolean JNICALL Java_app_organicmaps_sdk_routing_OnlineRouting_nativeUsesHardExclusions(JNIEnv *, jclass)
{ return RoutingManager::GetCarRoutingConfig().hardExclusions; }
JNIEXPORT jboolean JNICALL Java_app_organicmaps_sdk_routing_OnlineRouting_nativeConfigure(
    JNIEnv * env, jclass, jboolean enabled, jstring endpoint, jboolean exclusions)
{
  return g_framework->GetRoutingManager().SetCarRoutingConfig(
      {static_cast<bool>(enabled), jni::ToNativeString(env, endpoint), static_cast<bool>(exclusions)});
}
JNIEXPORT jboolean JNICALL Java_app_organicmaps_sdk_routing_OnlineRouting_nativeCanNavigate(JNIEnv *, jclass)
{ return g_framework->GetRoutingManager().CanFollowRoute(); }
JNIEXPORT jintArray JNICALL Java_app_organicmaps_sdk_routing_OnlineRouting_nativeGetStatus(JNIEnv * env, jclass)
{
  auto & manager = g_framework->GetRoutingManager();
  auto const info = manager.RoutingSession().GetCarRouteInfo();
  auto const failure = manager.GetCarRoutingFailure();
  jint const values[] = {static_cast<jint>(info.backend),
      static_cast<jint>(failure == routing::CarRoutingFailure::None ? info.fallback : manager.GetCarRoutingFailureReason()),
      static_cast<jint>(failure)};
  auto result = env->NewIntArray(3);
  env->SetIntArrayRegion(result, 0, 3, values);
  return result;
}
}
