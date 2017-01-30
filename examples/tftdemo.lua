
if dispType == nil then
	dispType = tft.ILI9341
	-- dispType = tft.XADOW_V1
	-- dispType = tft.ST7735B -- probably will work
	-- dispType = tft.ST7735  -- try if not
	-- dispType = tft.ST7735G -- or this one
end

tft.init(dispType,tft.PORTRAIT_FLIP)
if tft.gettype() < 0 then
	print("LCD not initialized")
	return
end

fontnames = {
	tft.FONT_DEFAULT,
	tft.FONT_7SEG,
	"/@font/DejaVuSans18.fon",
	"/@font/DotMatrix_M.fon",
	"/@font/OCR_A_Extended_M.fon"
}

-- print display header
function header(tx)
	math.randomseed(os.time())
	maxx, maxy = tft.getscreensize()
	tft.clear()
	tft.setcolor(tft.CYAN)
	if maxx < 240 then
		tft.setfont("/@font/SmallFont.fon")
	else
		tft.setfont(tft.FONT_DEFAULT)
	end
	miny = tft.getfontheight() + 5
	tft.rect(0,0,maxx-1,miny-1,tft.OLIVE,{8,16,8})
	tft.settransp(1)
	tft.write(tft.CENTER,2,tx)
	tft.settransp(0)
end

-- Display available fonts
function dispFont(sec)
	header("DISPLAY FONTS")

	local tx
	if maxx < 240 then
		tx = "ESP-Lua"
	else
		tx = "Hi from LoBo"
	end
	local starty = miny + 4

	local x,y
	local n = os.clock() + sec
	while os.clock() < n do
		y = starty
		x = 0
		local i,j
		for i=1, 3, 1 do
			for j=1, #fontnames, 1 do
				tft.setcolor(math.random(0xFFFF))
				tft.setfont(fontnames[j])
				if j ~= 2 then
					tft.write(x,y,tx)
				else
					tft.write(x,y,"-12.45/")
				end
				y = y + tft.getfontheight()
				if y > (maxy-tft.getfontheight()) then
					break
				end
			end
			y = y + 2
			if y > (maxy-tft.getfontheight()) then
				break
			end
			if i == 1 then 
				x = tft.CENTER
			end
			if i == 2 then
				x = tft.RIGHT
			end
		end
	end
end

function fontDemo(sec, rot)
	local tx = "FONTS"
	if rot > 0 then
		tx = "ROTATED "..tx
	end
	header(tx)

	tft.setclipwin(0,miny,maxx,maxy)
	tx = "ESP32-Lua"
	local x, y, color, i
	local n = os.clock() + sec
	while os.clock() < n do
		if rot == 1 then
			tft.setrot(math.floor(math.random(359)/5)*5);
		end
		for i=1, #fontnames, 1 do
			if (rot == 0) or (i ~= 1) then
				tft.setcolor(math.random(0xFFFF))
				tft.setfont(fontnames[i])
				x = math.random(maxx-8)
				y = math.random(miny, maxy-tft.getfontheight())
				if i ~= 2 then
					tft.write(x,y,tx)
				else
					tft.write(x,y,"-12.45/")
				end
			end
		end
	end
	tft.resetclipwin()
	tft.setrot(0)
end

function lineDemo(sec)
	header("LINE DEMO")

	tft.setclipwin(0,miny,maxx,maxy)
	local x1, x2,y1,y2,color
	local n = os.clock() + sec
	while os.clock() < n do
		x1 = math.random(maxx-4)
		y1 = math.random(miny, maxy-4)
		x2 = math.random(maxx-1)
		y2 = math.random(miny, maxy-1)
		color = math.random(0xFFFF)
		tft.line(x1,y1,x2,y2,color)
	end;
	tft.resetclipwin()
end;

function circleDemo(sec,dofill)
	local tx = "CIRCLE"
	if dofill > 0 then
		tx = "FILLED "..tx
	end
	header(tx)

	tft.setclipwin(0,miny,maxx,maxy)
	local x, y, r, color, fill
	local n = os.clock() + sec
	while os.clock() < n do
		x = math.random(4, maxx-2)
		y = math.random(miny+2, maxy-2)
		if x < y then
			r = math.random(2, x)
		else
			r = math.random(2, y)
		end
		color = math.random(0xFFFF)
		if dofill > 0 then
			fill = math.random(0xFFFF)
			tft.circle(x,y,r,color,fill)
		else
			tft.circle(x,y,r,color)
		end
	end;
	tft.resetclipwin()
end;

function rectDemo(sec,dofill)
	local tx = "RECTANGLE"
	if dofill > 0 then
		tx = "FILLED "..tx
	end
	header(tx)

	tft.setclipwin(0,miny,maxx,maxy)
	local x, y, w, h, color, fill
	local n = os.clock() + sec
	while os.clock() < n do
		x = math.random(4, maxx-2)
		y = math.random(miny, maxy-2)
		w = math.random(2, maxx-x)
		h = math.random(2, maxy-y)
		color = math.random(0xFFFF)
		if dofill > 0 then
			fill = math.random(0xFFFF)
			tft.rect(x,y,w,h,color,fill)
		else
			tft.rect(x,y,w,h,color)
		end
	end;
	tft.resetclipwin()
end;

function triangleDemo(sec,dofill)
	local tx = "TRIANGLE"
	if dofill > 0 then
		tx = "FILLED "..tx
	end
	header(tx)

	tft.setclipwin(0,miny,maxx,maxy)
	local x1, y1, x2, y2, x3, y3, color, fill
	local n = os.clock() + sec
	while os.clock() < n do
		x1 = math.random(4, maxx-2)
		y1 = math.random(miny, maxy-2)
		x2 = math.random(4, maxx-2)
		y2 = math.random(miny,maxy-2)
		x3 = math.random(4, maxx-2)
		y3 = math.random(miny, maxy-2)
		color = math.random(0xFFFF)
		if dofill > 0 then
			fill = math.random(0xFFFF)
			tft.triangle(x1,y1,x2,y2,x3,y3,color,fill)
		else
			tft.triangle(x1,y1,x2,y2,x3,y3,color)
		end
	end;
	tft.resetclipwin()
end;

function pixelDemo(sec)
	header("PUTPIXEL")

	tft.setclipwin(0,miny,maxx,maxy)
	local x, y, color
	local n = os.clock() + sec
	while os.clock() < n do
		x = math.random(maxx-1)
		y = math.random(miny, maxy-1)
		color = math.random(0xFFFF)
		tft.putpixel(x,y,color)
	end;
	tft.resetclipwin()
end

function imageDemo(sec)
	header("RAW IMAGE")
	tft.setcolor(tft.GREEN)
	if (maxx > maxy) then
		if os.exists("nature_160x123.img") ~= 0 then
			tft.image(tft.CENTER,tft.CENTER,160,123,"nature_160x123.img")
		else
			tft.write(tft.CENTER,tft.CENTER,"Image not found")
		end
	else
		if os.exists("newyear_128x96.img") ~= 0 then
			tft.image(tft.CENTER,tft.CENTER,128,96,"newyear_128x96.img")
		else
			tft.write(tft.CENTER,tft.CENTER,"Image not found")
		end
	end
	tmr.delayms(2000)

	header("JPG IMAGE")
	if os.exists("tiger240.jpg") ~= 0 then
		tft.jpgimage(0,miny+2,1,"tiger240.jpg")
	else
		tft.write(tft.CENTER,tft.CENTER,"Image not found")
	end
	tmr.delayms(2000)

	header("BMP IMAGE")
	if os.exists("tiger.bmp") ~= 0 then
		tft.bmpimage(0,miny+2,"tiger.bmp")
	else
		tft.write(tft.CENTER,tft.CENTER,"Image not found")
	end
	tmr.delayms(2000)
end

function intro(sec)
	maxx, maxy = tft.getscreensize()
	local inc = 360 / maxy
	local i
	for i=0,maxy-1,1 do
		tft.line(0,i,maxx-1,i,tft.hsb2rgb(i*inc,1,1))
	end
	tft.setrot(0);
	tft.setfont("/@font/DotMatrix_M.fon")
	local y = (maxy/2) - (tft.getfontheight() // 2)
	tft.settransp(1)
	
	tft.setcolor({1,1,1})
	tft.write(50, y,"ESP32-Lua")
	tft.setcolor(tft.WHITE)
	tft.write(51, y+1,"ESP32-Lua")
	
	y = y + tft.getfontheight()
	
	tft.setcolor({1,1,1})
	tft.write(60,y+2,"TFT demo")
	tft.setcolor(tft.WHITE)
	tft.write(61,y+3,"TFT demo")
	tft.settransp(0)
	tft.setcolor(tft.GREEN)
	tmr.delay(sec)
end

function lcdDemo(sec, orient)
	tft.setorient(orient)

	intro(sec)
	dispFont(sec)
	tmr.delayms(2000)
	fontDemo(sec,0)
	tmr.delayms(2000)
	fontDemo(sec,1)
	tmr.delayms(2000)
	lineDemo(sec,1)
	tmr.delayms(2000)
	circleDemo(sec,0)
	tmr.delayms(2000)
	circleDemo(sec,1)
	tmr.delayms(2000)
	rectDemo(sec,0)
	tmr.delayms(2000)
	rectDemo(sec,1)
	tmr.delayms(2000)
	triangleDemo(sec,0)
	tmr.delayms(2000)
	triangleDemo(sec,1)
	tmr.delayms(2000)
	pixelDemo(sec, orient)
	tmr.delayms(2000)
	imageDemo(sec)
	tmr.delayms(1000)
end

function fullDemo(sec, rpt)
	while rpt > 0 do
		tft.setrot(0);
		tft.setcolor(tft.CYAN)
		tft.setfont(tft.FONT_DEFAULT)

		lcdDemo(sec, tft.LANDSCAPE)
		tmr.delayms(5000)
		lcdDemo(sec, tft.PORTRAIT_FLIP)
		tmr.delayms(5000)

		tft.setcolor(tft.YELLOW)
		tft.write(tft.CENTER,maxy-tft.getfontheight() - 4,"That's all folks!")
		rpt = rpt - 1
	end
end

-- FUNCTION TO BE RUN IN THREAD!
function thfullDemo()
	local orientation = 0
	local sec = 5
	while true do
		tft.setrot(0);
		tft.setcolor(tft.CYAN)
		tft.setfont(tft.FONT_DEFAULT)

		lcdDemo(sec, orientation)

		tft.setcolor(tft.YELLOW)
		tft.write(tft.CENTER,maxy-tft.getfontheight() - 4,"That's all folks!")

		orientation = (orientation + 1) & 3
	end
end

header("ESP32-Lua")

--[[ EXAMPLES

circleDemo(6,1)
fullDemo(6, 1)

Also can be run in thread:
thread.start(thfullDemo)

--]]
