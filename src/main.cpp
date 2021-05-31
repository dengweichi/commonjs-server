#include <iostream>
#include "v8.h"
#include "libplatform/libplatform.h"
#include <unistd.h>
#include <algorithm>
#include <sstream>
#include <iterator>
#include<fstream>

// 当前模块的绝对路径
v8::Persistent<v8::String> currentModuleId;
// 模块缓存
v8::Persistent<v8::Object> cache;

std::string getAbsolutePath(const std::string& path,
                            const std::string& dir_name) {
    std::string absolute_path;
    // 判断是否为绝对路径。在linux 上下。文件以 / 开头
    if ( path[0] == '/') {
        absolute_path = path;
    } else {
        absolute_path = dir_name + '/' + path;
    }
    std::replace(absolute_path.begin(), absolute_path.end(), '\\', '/');
    std::vector<std::string> segments;
    std::istringstream segment_stream(absolute_path);
    std::string segment;
    while (std::getline(segment_stream, segment, '/')) {
        if (segment == "..") {
            segments.pop_back();
        } else if (segment != ".") {
            segments.push_back(segment);
        }
    }
    std::ostringstream os;
    std::copy(segments.begin(), segments.end() - 1,
              std::ostream_iterator<std::string>(os, "/"));
    os << *segments.rbegin();
    return os.str();
}

/**
 * 读取文件
 * @param path
 * @return
 */
v8::Local<v8::String>  readFile (std::string& path) {
    v8::Isolate* isolate = v8::Isolate::GetCurrent();
    // 读取主模块文件
    std::ifstream in(path.c_str());
    // 如果打开文件失败
    if (!in.is_open()) {
        isolate->ThrowException(v8::String::NewFromUtf8Literal(isolate, "无法打开文件"));
        return v8::Local<v8::String>();
    }
    std::string source;
    char buffer[256];
    while(!in.eof()){
        in.getline(buffer,256);
        source.append(buffer);
    };
    return v8::String::NewFromUtf8(isolate, source.c_str()).ToLocalChecked();
}

/**
 * 判断字符串是否以某个 后缀为结尾
 * @param str
 * @param suffix
 * @return
 */
inline bool has_suffix(const std::string &str, const std::string &suffix){
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}
/**
 * require 函数的实现
 * @param info
 */
void require(const v8::FunctionCallbackInfo<v8::Value> &info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.Length()) {
        info.GetReturnValue().Set(isolate->ThrowException(v8::String::NewFromUtf8Literal(isolate, "需要一个参数")));
        return;
    }
    if (!info[0]->IsString()) {
        info.GetReturnValue().Set(isolate->ThrowException(v8::String::NewFromUtf8Literal(isolate, "require的表示为字符串")));
        return;
    }

    v8::Local<v8::Object> moduleCache = v8::Local<v8::Object>::New(isolate, cache);
    v8::Local<v8::String> moduleId = v8::Local<v8::String>::New(isolate, currentModuleId);
    std::string modulePath(*v8::String::Utf8Value(isolate, info[0].As<v8::String>()));
    if (!has_suffix(modulePath, std::string(".js"))) {
        modulePath.append(".js");
    }

    std::string  moduleDir(*v8::String::Utf8Value(isolate, moduleId));
    if (moduleDir.find(std::string("."))  != -1) {
        int index = moduleDir.find_last_of("/");
        moduleDir = moduleDir.substr(0, index);
    }
    std::string moduleAbsolutePath = getAbsolutePath(modulePath, moduleDir);

    // 查找缓存
    v8::Local<v8::Value> module = moduleCache->Get(context, v8::String::NewFromUtf8(isolate, moduleAbsolutePath.c_str()).ToLocalChecked()).ToLocalChecked();
    // 如果命中缓存，直接使用缓存
    // 这里注意的是，在javaScript 获取一个属性的时候如果属性不存在，返回的是 undefined
    if (!module.IsEmpty() && !module->IsUndefined()){
        info.GetReturnValue().Set(module);
        return;
    }
    // 读取原文件
    v8::Local<v8::String> source = readFile(moduleAbsolutePath);
    // 构建脚本
    v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();

    // 把当前文件作为当前模块id
    currentModuleId.Reset(isolate, v8::String::NewFromUtf8(isolate, moduleAbsolutePath.c_str()).ToLocalChecked());
    // 执行模块
    script->Run(context).ToLocalChecked();
    module = moduleCache->Get(context, v8::String::NewFromUtf8(isolate, moduleAbsolutePath.c_str()).ToLocalChecked()).ToLocalChecked();
    if (!module.IsEmpty() && !module->IsUndefined()) {
        v8::Local<v8::Object> exports = module.As<v8::Object>()->Get(context, v8::String::NewFromUtf8Literal(isolate, "exports")).ToLocalChecked().As<v8::Object>();
        if (!exports.IsEmpty() && !exports->IsUndefined()) {
            info.GetReturnValue().Set(exports);
            return;
        }
    }
    info.GetReturnValue().SetNull();
}

/**
 * 异步获取模块
 * @param info
 */
void async(const v8::FunctionCallbackInfo<v8::Value> &info) {

}

/**
 * 全局对象define 的实现
 * @param info
 */
void define(const v8::FunctionCallbackInfo<v8::Value> &info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.Length()) {
        info.GetReturnValue().Set(isolate->ThrowException(v8::String::NewFromUtf8Literal(isolate, "只是需要一个参数")));
        return;
    }
    v8::Local<v8::Object> moduleCache = v8::Local<v8::Object>::New(isolate, cache);
    v8::Local<v8::String> moduleId = v8::Local<v8::String>::New(isolate, currentModuleId);
    // 构建 module对象， module.exports对象
    v8::Local<v8::Object> module = v8::Object::New(isolate);
    module->Set(context, v8::String::NewFromUtf8Literal(isolate, "uri"), moduleId).FromJust();
    // 把模块设置到缓存里面
    moduleCache->Set(context, moduleId, module).FromJust();
    if (info[0]->IsFunction()) {
        v8::Local<v8::Object> exports = v8::Object::New(isolate);
        module->Set(context, v8::String::NewFromUtf8Literal(isolate, "exports"), exports).FromJust();
        v8::Local<v8::Function> moduleCallBack = info[0].As<v8::Function>();
        // 构建require 函数
        v8::Local<v8::Function> requireFun =  v8::Function::New(context, require).ToLocalChecked();
        // 为require 函数添加 async 函数属性
        requireFun->Set(context, v8::String::NewFromUtf8Literal(isolate, "async"), v8::Function::New(context, async).ToLocalChecked()).FromJust();
        v8::Local<v8::Value> argv[] = { requireFun, exports, module};
        moduleCallBack->Call(context, context->Global(), 3, argv).ToLocalChecked();
    } else {
        module->Set(context, v8::String::NewFromUtf8Literal(isolate, "exports"), info[0]).FromJust();
    }
}



/**
 *  启动函数，参数0 为程序的名称 参数二为主模块
 * @param args
 * @param argv
 * @return
 */
int main(int args, char** argv) {
    // 如果没有入口文件
    if (argv[1] == nullptr) {
        return 1;
    }
    char workDirBuffer[255];
    // linux 获取工作目录
    getcwd(workDirBuffer,sizeof(workDirBuffer));

    // 初始化v8
    v8::V8::InitializeICUDefaultLocation(argv[0]);
    v8::V8::InitializeExternalStartupData(argv[0]);
    std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    v8::V8::InitializePlatform(platform.get());
    v8::V8::Initialize();
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
    v8::Isolate* isolate = v8::Isolate::New(create_params);

    {
        v8::Isolate::Scope isolate_scope(isolate);
        v8::HandleScope handleScope(isolate);

        // 设置微任务队列策略为 手动
        isolate->SetMicrotasksPolicy(v8::MicrotasksPolicy::kExplicit);
        v8::Local<v8::Context> context = v8::Context::New(isolate);
        v8::Context::Scope context_scope(context);

        // 初始化当前模块和 缓存模块
        currentModuleId.Reset(isolate, v8::String::NewFromUtf8(isolate, workDirBuffer).ToLocalChecked());
        cache.Reset(isolate, v8::Object::New(isolate));

        // 设置全局函数 define
        context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "define"), v8::Function::New(context, define).ToLocalChecked()).FromJust();
        // 设置全局函数用于打印结果
        context->Global()->Set(context, v8::String::NewFromUtf8Literal(isolate, "print"),
                               v8::Function::New(context, [](const v8::FunctionCallbackInfo <v8::Value> &info) -> void {
                                   v8::Isolate *isolate = info.GetIsolate();
                                   v8::Local<v8::Context> context = isolate->GetCurrentContext();
                                   // 如果是普通的对象，直接转成JSON字符串。如果是其他类型强制转换成字符串输出。
                                   if (!info[0]->IsNull() && info[0]->IsObject() && !info[0]->IsFunction()) {
                                       std::cout << *v8::String::Utf8Value(isolate, v8::JSON::Stringify(context, info[0]).ToLocalChecked()) << std::endl;
                                   } else {
                                       std::cout << *v8::String::Utf8Value(isolate, info[0].As<v8::String>()) << std::endl;
                                   }
                               }).ToLocalChecked()).FromJust();
        // 创建require 函数
        v8::Local<v8::Function> requireFun = v8::Function::New(context, require).ToLocalChecked();
        v8::Local<v8::Value> args[] = { v8::String::NewFromUtf8(isolate, argv[1]).ToLocalChecked() };
        // 加载主模块
        requireFun->Call(context, context->Global(), 1, args).ToLocalChecked();
    }
    isolate->Dispose();
    v8::V8::Dispose();
    v8::V8::ShutdownPlatform();
    delete create_params.array_buffer_allocator;
    return 0;
}