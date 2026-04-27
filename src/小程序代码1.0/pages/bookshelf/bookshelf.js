// pages/bookshelf/bookshelf.js
// 书架页面 - 从 OBS 读取所有图书数据并展示

// ============== OBS 配置（与主页面一致）==============
const OBS_BUCKET = 'ka1shu1-alot';
const OBS_REGION = 'cn-east-3';
const OBS_ENDPOINT = 'obs.' + OBS_REGION + '.myhuaweicloud.com';

Page({
    data: {
        loading: true,
        bookList: [],
        totalBooks: 0,
        onShelfCount: 0,
        borrowedCount: 0,
        showDetail: false,
        detailBook: null,
    },

    onLoad: function() {
        this.loadBooks();
    },

    onShow: function() {
        this.loadBooks();
    },

    // ============== 加载所有图书 ==============
    loadBooks: function() {
        var that = this;
        this.setData({ loading: true });

        this.obsList('book/', function(files) {
            if (!files || files.length === 0) {
                that.setData({ loading: false, bookList: [], totalBooks: 0, onShelfCount: 0, borrowedCount: 0 });
                return;
            }

            var readCount = files.length;
            var loaded = 0;
            var allBooks = [];

            for (var i = 0; i < readCount; i++) {
                (function(idx) {
                    that.obsGet(files[idx].key, function(book) {
                        if (book && book.isbn) {
                            // 格式化数据
                            book.displayTitle = book.title && book.title !== book.isbn ? book.title : (book.isbn || '未知');
                            book.displayAuthor = book.author || '';
                            book.statusText = book.currentStatus === 'on_shelf' ? '在架' : '借出';
                            book.statusClass = book.currentStatus === 'on_shelf' ? 'on-shelf' : 'borrowed';
                            book.readingDisplay = that.formatReadingTime(book.totalReadingSec || 0);
                            book.borrowCountText = (book.borrowCount || 0) + '次借出';

                            // 最近一次借还时间
                            if (book.history && book.history.length > 0) {
                                var last = book.history[book.history.length - 1];
                                book.lastEventTime = that.formatTime(last.timestamp);
                                book.lastEventType = last.type === 'returned' ? '归还' : '借出';
                                // 预计算历史记录的显示文本
                                for (var h = 0; h < book.history.length; h++) {
                                    var evt = book.history[h];
                                    evt.typeText = evt.type === 'returned' ? '归还' : '借出';
                                    evt.timeDisplay = that.formatTime(evt.timestamp);
                                    evt.durationDisplay = evt.durationSec ? '阅读 ' + that.formatDuration(evt.durationSec) : '';
                                }
                            } else {
                                book.lastEventTime = '--';
                                book.lastEventType = '--';
                            }

                            allBooks.push(book);
                        }
                        loaded++;
                        if (loaded === readCount) {
                            var onShelf = 0, borrowed = 0;
                            for (var j = 0; j < allBooks.length; j++) {
                                if (allBooks[j].currentStatus === 'on_shelf') onShelf++;
                                else borrowed++;
                            }
                            that.setData({
                                loading: false,
                                bookList: allBooks,
                                totalBooks: allBooks.length,
                                onShelfCount: onShelf,
                                borrowedCount: borrowed
                            });
                        }
                    });
                })(i);
            }
        });
    },

    // ============== 点击图书卡片查看详情 ==============
    onBookTap: function(e) {
        var book = this.data.bookList[e.currentTarget.dataset.index];
        this.setData({ showDetail: true, detailBook: book });
    },

    // ============== 关闭详情 ==============
    closeDetail: function() {
        this.setData({ showDetail: false, detailBook: null });
    },

    // ============== 刷新 ==============
    onRefresh: function() {
        this.loadBooks();
    },

    // ============== 格式化阅读时长 ==============
    formatReadingTime: function(sec) {
        if (!sec || sec <= 0) return '0m';
        if (sec >= 3600) {
            return (sec / 3600).toFixed(1) + 'h';
        }
        return Math.round(sec / 60) + 'm';
    },

    // ============== 格式化时间 ==============
    formatTime: function(timestamp) {
        if (!timestamp) return '--';
        var date = new Date(timestamp);
        return (date.getMonth() + 1) + '月' + date.getDate() + '日 ' +
            date.getHours().toString().padStart(2, '0') + ':' +
            date.getMinutes().toString().padStart(2, '0');
    },

    // ============== 格式化历史记录时长 ==============
    formatDuration: function(sec) {
        if (!sec || sec <= 0) return '';
        if (sec >= 3600) {
            return Math.round(sec / 3600) + '小时';
        }
        return Math.round(sec / 60) + '分钟';
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
                    } catch (e) {
                        callback(null);
                    }
                } else {
                    callback(null);
                }
            },
            fail: function() {
                callback(null);
            }
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
                } else {
                    callback([]);
                }
            },
            fail: function() {
                callback([]);
            }
        });
    },
});
