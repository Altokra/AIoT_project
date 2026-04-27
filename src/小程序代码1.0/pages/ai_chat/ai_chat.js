// pages/ai_chat/ai_chat.js
// AI 对话页面 - 基于 DeepSeek API，支持书架知识库

const API_KEY = 'sk-b6e5c8309b4d45b8ba1572b5b47b11b7';
const API_URL = 'https://api.deepseek.com/v1/chat/completions';
const OBS_BUCKET = 'ka1shu1-alot';
const OBS_REGION = 'cn-east-3';
const OBS_ENDPOINT = 'obs.' + OBS_REGION + '.myhuaweicloud.com';

Page({
    data: {
        inputText: '',
        messages: [],
        thinking: false,
        bookContext: '',  // 书架知识库文本
        contextLoaded: false,
    },

    onLoad: function() {
        this.loadBookContext();
    },

    // ============== 加载书架知识库 ==============
    loadBookContext: function() {
        var that = this;
        this.obsList('book/', function(files) {
            if (!files || files.length === 0) {
                that.initWelcome();
                return;
            }
            var readCount = Math.min(files.length, 20);
            var loaded = 0;
            var bookInfos = [];

            for (var i = 0; i < readCount; i++) {
                (function(idx) {
                    that.obsGet(files[idx].key, function(book) {
                        if (book && book.isbn) {
                            var status = book.currentStatus === 'on_shelf' ? '在架' : '借出';
                            var title = book.title && book.title !== book.isbn ? book.title : '未知书名';
                            var author = book.author || '未知作者';
                            bookInfos.push(title + '（作者:' + author + '，状态:' + status + '，借出' + (book.borrowCount || 0) + '次）');
                        }
                        loaded++;
                        if (loaded === readCount) {
                            if (bookInfos.length > 0) {
                                that.setData({
                                    bookContext: '当前书架共有 ' + bookInfos.length + ' 本书，列表如下：' + bookInfos.join('；')
                                });
                            }
                            that.initWelcome();
                        }
                    });
                })(i);
            }
        });
    },

    // ============== 初始化欢迎语 ==============
    initWelcome: function() {
        var ctx = this.data.bookContext;
        var welcome = ctx
            ? '你好！我是智能图书馆的 AI 助手。当前书架情况：' + ctx + '。你可以问我关于图书、设备操作、湿度控制等问题。'
            : '你好！我是智能图书馆的 AI 助手。你可以问我关于图书的问题。';
        this.setData({
            messages: [{ role: 'assistant', content: welcome }],
            contextLoaded: true
        });
    },

    // ============== 发送消息 ==============
    onSend: function() {
        var text = this.data.inputText.trim();
        if (!text || this.data.thinking) return;

        var msgs = this.data.messages;
        msgs.push({ role: 'user', content: text });
        this.setData({ messages: msgs, inputText: '', thinking: true });

        this.callDeepSeek(msgs);
    },

    // ============== 输入框变化 ==============
    onInputChange: function(e) {
        this.setData({ inputText: e.detail.value });
    },

    // ============== 清空对话 ==============
    onClear: function() {
        this.loadBookContext();
    },

    // ============== DeepSeek API 调用 ==============
    callDeepSeek: function(messages) {
        var that = this;
        var systemContent = '你是一个智能图书馆助手，正在回答用户关于图书馆的问题。';
        var ctx = this.data.bookContext;
        if (ctx) {
            systemContent += '书架知识库：' + ctx + '。';
        }
        var apiMsgs = [{ role: 'system', content: systemContent }];
        messages.forEach(function(m) {
            apiMsgs.push({ role: m.role, content: m.content });
        });

        wx.request({
            url: API_URL,
            method: 'POST',
            header: {
                'Content-Type': 'application/json',
                'Authorization': 'Bearer ' + API_KEY
            },
            data: {
                model: 'deepseek-chat',
                messages: apiMsgs,
                max_tokens: 600,
                temperature: 0.7
            },
            success: function(res) {
                if (res.statusCode === 200 && res.data && res.data.choices && res.data.choices[0]) {
                    var reply = res.data.choices[0].message.content;
                    var updated = that.data.messages;
                    updated.push({ role: 'assistant', content: reply });
                    that.setData({ messages: updated, thinking: false });
                } else {
                    that.addError('API 响应异常');
                }
            },
            fail: function(err) {
                that.addError('网络错误: ' + (err.errMsg || ''));
            }
        });
    },

    // ============== 添加错误消息 ==============
    addError: function(text) {
        var msgs = this.data.messages;
        msgs.push({ role: 'assistant', content: '⚠️ ' + text });
        this.setData({ messages: msgs, thinking: false });
    },

    // ============== OBS GET ==============
    obsGet: function(key, callback) {
        wx.request({
            url: 'https://' + OBS_BUCKET + '.' + OBS_ENDPOINT + '/' + key,
            method: 'GET',
            success: function(res) {
                if (res.statusCode === 200) {
                    try {
                        var data = typeof res.data === 'string' ? JSON.parse(res.data) : res.data;
                        callback(data);
                    } catch (e) { callback(null); }
                } else { callback(null); }
            },
            fail: function() { callback(null); }
        });
    },

    // ============== OBS 列举 ==============
    obsList: function(prefix, callback) {
        wx.request({
            url: 'https://' + OBS_BUCKET + '.' + OBS_ENDPOINT + '/?prefix=' + encodeURIComponent(prefix),
            method: 'GET',
            dataType: 'text',
            success: function(res) {
                if (res.statusCode === 200 && typeof res.data === 'string') {
                    var files = [];
                    var regex = /<Contents>([\s\S]*?)<\/Contents>/g;
                    var match;
                    while ((match = regex.exec(res.data)) !== null) {
                        var content = match[1];
                        var keyMatch = content.match(/<Key>([\s\S]*?)<\/Key>/);
                        var timeMatch = content.match(/<LastModified>([\s\S]*?)<\/LastModified>/);
                        if (keyMatch && !keyMatch[1].endsWith('/')) {
                            files.push({ key: keyMatch[1], lastModified: timeMatch ? timeMatch[1] : '' });
                        }
                    }
                    files.sort(function(a, b) { return (b.lastModified || '').localeCompare(a.lastModified || ''); });
                    callback(files);
                } else { callback([]); }
            },
            fail: function() { callback([]); }
        });
    },
});