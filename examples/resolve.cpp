#include <iostream>
#include <limits>

#include <js/Conversions.h>
#include <js/Initialization.h>
#include <jsapi.h>
#include <jsfriendapi.h>

namespace zlib {
#include <zlib.h>
};

/* This example illustrates how to set up a class with a custom resolve hook, in
 * order to do lazy property resolution.
 *
 * We'll use the CRC-32 checksum API from zlib as an example. Not because it's
 * an incredibly useful API, but zlib is already a dependency of SpiderMonkey,
 * so it's likely to be installed anywhere these examples are being compiled.
 *
 * There will be two properties that can resolve lazily: an `update()` method,
 * and a `checksum` property. */

class Crc {
  unsigned long m_crc;

  Crc(void) : m_crc(zlib::crc32(0L, nullptr, 0)) {}

  bool updateImpl(JSContext* cx, const JS::CallArgs& args) {
    if (!args.requireAtLeast(cx, "update", 1)) return false;

    if (!args[0].isObject() || !JS_IsUint8Array(&args[0].toObject())) {
      JS_ReportErrorASCII(cx, "argument to update() should be a Uint8Array");
      return false;
    }

    JSObject* buffer = &args[0].toObject();

    size_t len = JS_GetTypedArrayLength(buffer);
    if (len > std::numeric_limits<unsigned>::max()) {
      JS_ReportErrorASCII(cx, "array has too many bytes");
      return false;
    }

    {
      bool isSharedMemory;
      JS::AutoAssertNoGC nogc;
      uint8_t* data = JS_GetUint8ArrayData(buffer, &isSharedMemory, nogc);

      m_crc = zlib::crc32(m_crc, data, unsigned(len));
    }

    args.rval().setUndefined();
    return true;
  }

  bool getChecksumImpl(JSContext* cx, const JS::CallArgs& args) {
    args.rval().setNumber(uint32_t(m_crc));
    return true;
  }

  static Crc* getPriv(JSObject* obj) {
    return static_cast<Crc*>(JS_GetPrivate(obj));
  }

  static bool isPrototype(JSObject* obj) { return getPriv(obj) == nullptr; }

  static bool checkIsInstance(JSContext* cx, JSObject* obj, const char* what) {
    if (isPrototype(obj)) {
      JS_ReportErrorASCII(cx, "can't %s on Crc.prototype", what);
      return false;
    }
    return true;
  }

  static bool constructor(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);

    if (!args.isConstructing()) {
      JS_ReportErrorNumberASCII(cx, js::GetErrorMessage, nullptr,
                                JSMSG_CANT_CALL_CLASS_CONSTRUCTOR);
      return false;
    }

    JS::RootedObject newObj(cx,
                            JS_NewObjectForConstructor(cx, &Crc::klass, args));
    if (!newObj) return false;

    Crc* priv = new Crc();
    JS_SetPrivate(newObj, priv);

    args.rval().setObject(*newObj);
    return true;
  }

  static bool update(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject thisObj(cx, &args.computeThis(cx).toObject());
    if (!checkIsInstance(cx, thisObj, "call update()")) return false;
    return getPriv(thisObj)->updateImpl(cx, args);
  }

  static bool getChecksum(JSContext* cx, unsigned argc, JS::Value* vp) {
    JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
    JS::RootedObject thisObj(cx, &args.computeThis(cx).toObject());
    if (!checkIsInstance(cx, thisObj, "read checksum")) return false;
    return getPriv(thisObj)->getChecksumImpl(cx, args);
  }

  static bool newEnumerate(JSContext* cx, JS::HandleObject obj,
                           JS::AutoIdVector& properties, bool enumerableOnly) {
    // We only want to enumerate if obj is the prototype. For instances, we
    // should return immediately, and this will be called again on the
    // prototype.
    if (!isPrototype(obj)) return true;

    jsid idUpdate =
        INTERNED_STRING_TO_JSID(cx, JS_AtomizeAndPinString(cx, "update"));
    if (!properties.append(idUpdate)) return false;

    jsid idChecksum =
        INTERNED_STRING_TO_JSID(cx, JS_AtomizeAndPinString(cx, "checksum"));
    if (!properties.append(idChecksum)) return false;

    return true;
  }

  static bool resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId id,
                      bool* resolved) {
    // We only want to resolve if obj is the prototype. For instances, we should
    // return immediately, and this will be called again on the prototype.
    if (!isPrototype(obj)) {
      *resolved = false;
      return true;
    }

    if (!JSID_IS_STRING(id)) {
      *resolved = false;
      return true;
    }

    JSFlatString* str = JSID_TO_FLAT_STRING(id);

    if (JS_FlatStringEqualsAscii(str, "update")) {
      if (!JS_DefineFunctionById(cx, obj, id, &Crc::update, 1,
                                 JSPROP_ENUMERATE))
        return false;
      *resolved = true;
      return true;
    }

    if (JS_FlatStringEqualsAscii(str, "checksum")) {
      if (!JS_DefinePropertyById(cx, obj, id, &Crc::getChecksum, nullptr,
                                 JSPROP_ENUMERATE))
        return false;
      *resolved = true;
      return true;
    }

    *resolved = false;
    return true;
  }

  static bool mayResolve(const JSAtomState& names, jsid id,
                         JSObject* maybeObj) {
    if (!JSID_IS_STRING(id)) return false;

    JSFlatString* str = JSID_TO_FLAT_STRING(id);
    return JS_FlatStringEqualsAscii(str, "update") ||
           JS_FlatStringEqualsAscii(str, "checksum");
  }

  static void finalize(JSFreeOp* fop, JSObject* obj) {
    Crc* priv = getPriv(obj);
    if (priv) {
      delete priv;
      JS_SetPrivate(obj, nullptr);
    }
  }

  // Note that this vtable applies both to the prototype and instances. The
  // operations must distinguish between the two.
  static constexpr JSClassOps classOps = {
      nullptr,  // addProperty
      nullptr,  // deleteProperty
      nullptr,  // enumerate
      &Crc::newEnumerate,
      &Crc::resolve,
      &Crc::mayResolve,
      &Crc::finalize,
      nullptr,  // call
      nullptr,  // hasInstance
      nullptr,  // construct
      nullptr,  // trace
  };

  static constexpr JSClass klass = {
      "Crc",
      JSCLASS_HAS_PRIVATE | JSCLASS_BACKGROUND_FINALIZE,
      &Crc::classOps,
  };

 public:
  static bool DefinePrototype(JSContext* cx) {
    JS::RootedObject global(cx, JS::CurrentGlobalOrNull(cx));
    JS::RootedObject proto(
        cx, JS_InitClass(cx,
                         global,   // the object in which to define the class
                         nullptr,  // the prototype of the parent class
                                   // (in our case, no parent class)
                         &Crc::klass,  // the JSClass defined above
                         &Crc::constructor,
                         0,  // constructor and num. args
                         // The four nullptrs below are for arrays where you
                         // would list predefined (not lazy) methods and
                         // properties, static and non-static
                         nullptr, nullptr, nullptr, nullptr));
    if (!proto) return false;

    // Here's how we tell the prototype apart from instances. The private
    // pointer will be null.
    JS_SetPrivate(proto, nullptr);
    return true;
  }
};
constexpr JSClassOps Crc::classOps;
constexpr JSClass Crc::klass;

static const char* testProgram = R"js(
  const crc = new Crc();
  crc.update(new Uint8Array([1, 2, 3, 4, 5]));
  crc.checksum;
)js";

/**** BOILERPLATE *************************************************************/
// Below here, the code is very similar to what is found in hello.cpp

static JSClassOps globalOps = {nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               nullptr,
                               JS_GlobalObjectTraceHook};

// The class of the global object.
static JSClass globalClass = {"ResolveGlobal", JSCLASS_GLOBAL_FLAGS,
                              &globalOps};

static JSContext* CreateContext(void) {
  JSContext* cx = JS_NewContext(8L * 1024 * 1024);
  if (!cx) return nullptr;
  if (!JS::InitSelfHostedCode(cx)) return nullptr;
  return cx;
}

static JSObject* CreateGlobal(JSContext* cx) {
  JS::CompartmentOptions options;
  JS::RootedObject global(
      cx, JS_NewGlobalObject(cx, &globalClass, nullptr, JS::FireOnNewGlobalHook,
                             options));

  // Add standard JavaScript classes to the global so we have a useful
  // environment.
  JSAutoCompartment ac(cx, global);
  if (!JS_InitStandardClasses(cx, global)) return nullptr;

  return global;
}

static bool ExecuteCodePrintResult(JSContext* cx, const char* code) {
  JS::CompileOptions options(cx);
  options.setFileAndLine("noname", 1);
  JS::RootedValue rval(cx);
  if (!JS::Evaluate(cx, options, code, strlen(code), &rval)) return false;

  JS::RootedString rval_str(cx, JS::ToString(cx, rval));
  if (!rval_str) return false;

  std::cout << JS_EncodeString(cx, rval_str) << '\n';
  return true;
}

static void die(const char* why) {
  std::cerr << "fatal error: " << why;
  exit(1);
}

void LogException(JSContext* cx) {
  JS::RootedValue exception(cx);
  if (!JS_GetPendingException(cx, &exception))
    die("Uncatchable exception thrown, out of memory or something");

  JS_ClearPendingException(cx);

  JS::RootedString exc_str(cx, JS::ToString(cx, exception));
  if (!exc_str) die("Exception thrown, could not be converted to string");

  std::cout << "Exception thrown: " << JS_EncodeString(cx, exc_str) << '\n';
}

static bool Run(JSContext* cx) {
  JSAutoRequest ar(cx);

  JS::RootedObject global(cx, CreateGlobal(cx));
  if (!global) {
    LogException(cx);
    return false;
  }

  JSAutoCompartment ac(cx, global);

  if (!Crc::DefinePrototype(cx) || !ExecuteCodePrintResult(cx, testProgram)) {
    LogException(cx);
    return false;
  }

  return true;
}

int main(int argc, const char* argv[]) {
  if (!JS_Init()) die("Could not initialize JavaScript engine");

  JSContext* cx = CreateContext();
  if (!cx) die("Could not set up JavaScript context");

  if (!Run(cx)) return 1;

  JS_DestroyContext(cx);
  JS_ShutDown();
  return 0;
}
