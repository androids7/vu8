#include <vu8/Context.hpp>
#include <vu8/String.hpp>
#include <vu8/Throw.hpp>

#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>

#include <dlfcn.h>

namespace tsa { namespace vu8 {

v8::Handle<v8::Value> LoadModule(const v8::Arguments& args) {
    // std::cout << "brianny\n";
    if (1 != args.Length())
        return Throw("loadmodule: incorrect arguments");
    // std::cout << "brion\n";

    v8::HandleScope handle_scope;
    v8::String::Utf8Value str(args[0]);
    std::string modName = ToCString(str);
    // std::cout << "loading (" << modName << ")" << std::endl;

    v8::Handle<v8::Value> ctxtValue =
        args.Holder()->Get(v8::String::New("_vu8_context"));

    if (ctxtValue.IsEmpty())
        return Throw("loadmodule: context is set up incorrectly");

    Context& context =
        *reinterpret_cast<Context *>(v8::External::Unwrap(ctxtValue));

    typedef Context::modules_t modules_t;
    modules_t::iterator it = context.modules_.find(modName);
    // check if module is already loaded
    if (it != context.modules_.end())
        return boost::get<1>(it->second);

    std::string modPath = context.libPath_;
    modPath.append("/libvu8_").append(modName).append(".so");
    void *dl = dlopen(modPath.c_str(), RTLD_LAZY);

    if (! dl)
        return Throw("loadmodule: could not find shared library");

    // re-use modPath as entry name
    modPath = "vu8_module_";
    modPath.append(modName);

    void *sym = dlsym(dl, modPath.c_str());
    if (! sym)
        return Throw("loadmodule: initialisation function not found");

    v8::Handle<v8::Value> value =
        (*reinterpret_cast<Context::ModuleLoadCallback>(sym))();

    std::pair<modules_t::iterator, bool> ret =
        context.modules_.insert(modules_t::value_type(
            modName, boost::make_tuple(
                dl, v8::Persistent<v8::Value>::New(value))));

    return boost::get<1>(ret.first->second);
}

void Context::Init() {
    if (IsEmpty()) {
        template_->Set(v8::String::New("loadmodule"),
                       v8::FunctionTemplate::New(&LoadModule));
        template_->Set(v8::String::New("_vu8_context"), v8::External::New(this));

        context_ = v8::Context::New(0, template_);
        context_->Enter();
    }
}

void Context::RunFile(char const *filename) {
    Init();

    std::ifstream stream(filename);
    if (! stream) {
        std::string error = "could not locate file ";
        error.append(filename);
        throw std::runtime_error(error);
    }

    std::stringstream scriptStream;
    std::string line;
    while (! stream.eof()) {
        std::getline(stream, line);
        scriptStream << line << '\n';
    }

    v8::Handle<v8::Script> script =
        v8::Script::Compile(v8::String::New(scriptStream.str().c_str()));
    script->Run();
}

Context::Context(std::string const& libPath)
  : template_(v8::ObjectTemplate::New()), libPath_(libPath) {}

Context::~Context() {
    if (! IsEmpty()) {
        for (modules_t::iterator it = modules_.begin();
             it != modules_.end(); ++it)
        {
            boost::get<1>(it->second).Dispose();
            dlclose(boost::get<0>(it->second));
        }
        modules_.clear();
        context_->Exit();
        context_.Dispose();
    }
}

} }