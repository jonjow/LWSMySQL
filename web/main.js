
var mainAppClass = function () {
    console.group("main application entry point");
    this.construct();
};

mainAppClass.prototype = {
    construct: function () {
        var self = this;
        console.group("main application construct");
    }  
};

app = new mainAppClass();
