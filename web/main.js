
/*Display class*/

function create_line(attrs)
{
	var line = document.createElementNS(this.svgNS, "svg:line" );		 
	for(name in attrs){ line.setAttributeNS(null, name, attrs[name]); }
	this.area.appendChild(line);	
}

function draw_grid()
{
	for(i = 12.5; i < 100; i += 12.5)
	{
		if(i==50){ continue; }
		/*Vertical*/
		this.create_line({ "class": "grid", 
			"x1": i + "%",
			"y1": 0,
			"x2": i + "%",
			"y2": "100%" });
		/*Horizontal*/
		this.create_line({ "class": "grid", 
			"x1": 0,
			"y1": i + "%",
			"x2": "100%",
			"y2": i + "%" });
	}
	
	for(i = 2.5; i < 100; i += 2.5)
	{
		if(i%12.5 == 0){ continue; }
		/*Vertical*/
		this.create_line({ "class": "mark", 
			"x1": i + "%",
			"y1": "49%",
			"x2": i + "%",
			"y2": "51%" });
		/*Horizontal*/
		this.create_line({ "class": "mark", 
			"x1": "49%",
			"y1": i + "%",
			"x2": "51%",
			"y2": i + "%" });
	}
}

function adc_to_coord(val, vdiv)
{
	var ADC_min = 0;
	var ADC_max = 5;
	var precision = 256;
	
	var ppv = 500 / (vdiv * 8);
		
	return 250 - Math.round((val / precision) * ADC_max * ppv);
}
function time_to_coord(val, sr, tdiv)
{
	if(!val){ return 0;}
	var ppt = 500 / (tdiv * 8);
	return Math.round(( (1 / sr) * val ) * ppt);
}

function draw(channel)
{
	if(!channel.svg)
	{
		channel.svg = document.createElementNS(this.svgNS, "svg:polyline");
		channel.svg.setAttributeNS(null, "class", "waveform");
		this.area.appendChild(channel.svg);	
	}
	
	var style = "stroke: " + channel.get_style().color + ";" + channel.get_style().line_style;
	channel.svg.setAttributeNS(null, "style", style);
	
	var points = "";
	if(channel.sampling_size > 1)
	{
		for( i = 0; i < channel.sampling_size; i += 1 )
		{
			var x = time_to_coord(i, channel.sampling_rate.value , channel.tdiv);
			var y = adc_to_coord(channel.sample[i], channel.vdiv);
			points += x + "," + y + " ";
		}
	}
	
	channel.svg.setAttributeNS(null, "points", points);
}

function Display(area)
{
	this.area = area;
	this.draw_grid = draw_grid;
	this.draw = draw;
	this.create_line = create_line;
	this.svgNS = "http://www.w3.org/2000/svg";
}

var hdl; /* a poor hack...*/

function post_params()
{
	this.ajax_params = new XMLHttpRequest();
	hdl = this;
	this.ajax_params.onreadystatechange=function(){ hdl.rec_params(); }
	this.ajax_params.open("POST", "/ch" + this.number + "/pr", true);
	var settings = "";
	if(this.empty)
	{
		this.ajax_params.send();
		return;
	}
	var trigger_flags = 0;
	if(this.trigger_on.checked != "")
	{
		trigger_flags += 1;
	}
	if(this.trigger_up.checked != "")
	{
		trigger_flags += 2;
	}
	this.ajax_params.send("sr="+this.sampling_rate.value+"&tf="+trigger_flags+"&tl="+this.trigger_level.value);
}

function rec_params()
{
	if(this.ajax_params.readyState==4)
	{
		if(this.ajax_params.status==200 || this.ajax_params.status==304)
		{
			this.empty = false;
			var settings = JSON.parse(this.ajax_params.responseText);
			
			this.sampling_rate.value=settings.sr;
			this.trigger_level.value=settings.tl;
			if( settings.tf & 1 ){ this.trigger_on.checked="checked"; } 
			else { this.trigger_on.checked=""; }
			if( settings.tf & 2 )
			{
				this.trigger_up.checked="checked";
				this.trigger_down.checked="";
			} 
			else
			{
				this.trigger_up.checked="";
				this.trigger_down.checked="checked";
			} 
		}
	}
}

function apply_params()
{
	this.post_params();
	if(isNaN(this.sampling_rate.value)) { return; }
	if(isNaN(this.trigger_level.value)) { return; }
}

function get_sample()
{
	this.ajax_sample = new XMLHttpRequest();
	hdl = this;
	this.ajax_sample.onreadystatechange=function(){ hdl.rec_sample(); }
	this.ajax_sample.open("GET", "/ch" + this.number, true);
	this.ajax_sample.send();
}

function rec_sample()
{
	if(this.ajax_sample.readyState==4)
	{
		if(this.ajax_sample.status==200 || this.ajax_sample.status==304)
		{
			this.sample = [];
			this.sampling_size = this.ajax_sample.responseText.length; 
			for(i = 0; i < this.sampling_size; ++i)
			{
				this.sample[i] = this.ajax_sample.responseText[i].charCodeAt() & 0x00FF;
			}
			this.display.draw(this);
		}
		this.timer = setTimeout(function(o){ o.get_sample(); }, 200, this);
	}
}
function get_style()
{
	var style = { "color": this.color.value, "line_style": "" };
	
	switch(this.line_style.value)
	{
		case "dashed": style.line_style = "stroke-dasharray: 4,4"; break;
		case "bold": style.line_style = "stroke-width:4;"; break;
		default:
	}
	
	return style;
}

function vdiv_change()
{
	var val = this.vdivctl.value;
	if(isNaN(val)){	return; }
	this.vdiv = val;
	switch(this.vdivunit.value)
	{
		case "V" : this.vdiv /= 1; break;
		case "mV" : this.vdiv /= 1000; break;
		default:
	}
	this.display.draw(this);
}

function tdiv_change()
{
	this.tdiv = this.tdivctl.value;
	switch(this.tdivunit.value)
	{
		case "S" : this.tdiv /= 1; break;
		case "mS" : this.tdiv /= 1000; break;
		case "uS" : this.tdiv /= 1000000; break;
		default:
	}
	this.display.draw(this);
}

function Channel(number, control, display)
{
	this.number = number;
	this.post_params = post_params;
	this.rec_params= rec_params;
	this.get_sample = get_sample;
	this.rec_sample = rec_sample;
	this.apply_params = apply_params;
	this.display = display;
	this.vdiv_change = vdiv_change;
	this.tdiv_change = tdiv_change;
	this.get_style = get_style;
	this.timer = setTimeout( function(o){ o.get_sample(); }, 1000 * Math.random(), this ); /*Does not work with IE (http://klevo.sk/javascript/javascripts-settimeout-and-how-to-use-it-with-your-methods/)*/
	this.empty = true;
	this.control = control;
	this.vdivctl = control.children["vdiv"];
	this.vdivctl.addEventListener('change', function(){ channels[number - 1].vdiv_change(); }, false);
	this.vdiv = this.vdivctl.value;
	this.vdivunit = control.children["vdivunit"];
	this.vdivunit.addEventListener('change', function(){ channels[number - 1].vdiv_change(); }, false);
	this.tdivctl = control.children["tdiv"];
	this.tdivctl.addEventListener('change', function(){ channels[number - 1].tdiv_change(); }, false);
	this.tdiv = this.tdivctl.value / 1000;
	this.tdivunit = control.children["tdivunit"];
	this.tdivunit.addEventListener('change', function(){ channels[number - 1].tdiv_change(); }, false);
	this.sample = [];
	/*for(i = 0; i < 200; i += 1)
	{
		this.sample.push(Math.floor(Math.random()*201));
	}*/
	this.sampling_rate = control.children["sampling_rate"];
	this.sampling_size = 0;
	this.trigger_level = control.children["trigger_level"];
	this.trigger_on = control.children["trigger_on"];
	this.trigger_up = control.children["trigger_up"];
	this.trigger_down = control.children["trigger_down"];
	this.control.children["apply"].addEventListener('click', function(){ channels[number - 1].apply_params(); }, false);
	this.color = control.children["style_menu"].children["color"] ;
	this.line_style = control.children["style_menu"].children["line_style"];
	
	this.post_params();
}

var display;
var channels = [];


window.onload = function ()
{
	 display = new Display(document.getElementById("display"));
	 display.draw_grid();
	 channels.push( new Channel(1, document.getElementById("channel1"), display) );
	 /*channels.push( new Channel(2, document.getElementById("channel2"), display) );*/
}
