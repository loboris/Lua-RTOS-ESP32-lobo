--[[
Simple paint program to demonstrate lcd & touch on ILI9341 based displays
We are using Lua coroutines so that long running program does not block
Execute paint.run() to start the program
]]--

if lcd.gettype() ~= 1 then
    print("LCD not initialized or wrong type")
    return
end

paint = {}

paint.tmr_interval = 25
paint.orient = lcd.PORTRAIT_FLIP
paint.running = false
paint.waittouch = 0
-- create Coroutine timer in stopped mode with dummy cb -----
paint.tmr = timer.create(paint.tmr_interval, function() end, 2)
-- ----------------------------------------------------------

-- =================================================
-- ---------------------------
function paint.coYield(tpwait)
	paint.waittouch = tpwait
	
	if coroutine.running() ~= nil then
		-- Execute if running from within coroutine
		timer.resume(paint.tmr, 1)
		coroutine.yield()
	else
		-- Execute if NOT running from within coroutine
		local touch
		while true do
			touch, _, _ = lcd.gettouch()
			if ((touch > 0) and (paint.waittouch == 1)) or ((touch == 0) and (paint.waittouch == 0)) then
				break
			end
		end
	end
end
-- =================================================


-- Display selection bars and some info
-- ------------------------
function paint.paint_info()
  local dispx, dispy, dx, dy, dw, dh, fp

  lcd.setorient(paint.orient)
  lcd.setfont(lcd.FONT_DEFAULT)
  lcd.setrot(0)
  lcd.setfixed(0)
  dispx, dispy = lcd.getscreensize()
  dx = dispx / 8
  dw,dh = lcd.getfontsize()
  dy = dispy - dh - 5
  fp = math.ceil((dx / 2) - (dw/2))
  
  lcd.rect(dx*0,0,dx-2,18,lcd.BLACK,lcd.BLACK)
  lcd.rect(dx*1,0,dx-2,18,lcd.WHITE,lcd.WHITE)
  lcd.rect(dx*2,0,dx-2,18,lcd.RED,lcd.RED)
  lcd.rect(dx*3,0,dx-2,18,lcd.GREEN,lcd.GREEN)
  lcd.rect(dx*4,0,dx-2,18,lcd.BLUE,lcd.BLUE)
  lcd.rect(dx*5,0,dx-2,18,lcd.YELLOW,lcd.YELLOW)
  lcd.rect(dx*6,0,dx-2,18,lcd.CYAN,lcd.CYAN)
  lcd.rect(dx*7,0,dx-2,18,lcd.ORANGE,lcd.ORANGE)

  lcd.rect(dx*7+2,2,dx-6,14,lcd.WHITE, lcd.ORANGE)
  lcd.rect(dx*7+3,3,dx-8,12,lcd.BLACK, lcd.ORANGE)
  
  lcd.rect(dx*0,dy,dx-2,dispy-dy,lcd.DARKGREY)
  lcd.rect(dx*1,dy,dx-2,dispy-dy,lcd.YELLOW)
  lcd.rect(dx*2,dy,dx-2,dispy-dy,lcd.DARKGREY)
  lcd.rect(dx*3,dy,dx-2,dispy-dy,lcd.DARKGREY)
  lcd.rect(dx*4,dy,dx-2,dispy-dy,lcd.DARKGREY)
  lcd.rect(dx*5,dy,dx-2,dispy-dy,lcd.DARKGREY)
  lcd.rect(dx*6,dy,dx-2,dispy-dy,lcd.DARKGREY)
  lcd.rect(dx*7,dy,dx-2,dispy-dy,lcd.DARKGREY)
  
  lcd.setcolor(lcd.CYAN)
  lcd.write(dx*0+fp,dy+3,"2")
  lcd.write(dx*1+fp,dy+3,"4")
  lcd.write(dx*2+fp,dy+3,"6")
  lcd.write(dx*3+fp,dy+3,"8")
  lcd.write(dx*4+fp-3,dy+3,"10")
  --lcd.write(dx*5+fp,dy+3,"S")
  lcd.circle(dx*5+((dx-2)/2), dy+((dispy-dy)/2), (dispy-dy)/2-2, lcd.LIGHTGREY, lcd.LIGHTGREY)
  lcd.write(dx*6+fp,dy+3,"C")
  lcd.write(dx*7+fp,dy+3,"R")
  
  lcd.setcolor(lcd.YELLOW)
  lcd.write(60,40,"S")
  lcd.setcolor(lcd.CYAN)
  lcd.write(lcd.LASTX,lcd.LASTY," change shape")
  lcd.circle(60+(dw/2), 40+(dh/2), dh/2+1, lcd.LIGHTGREY, lcd.LIGHTGREY)

  lcd.setcolor(lcd.YELLOW)
  lcd.write(60,dh*2+40,"C")
  lcd.setcolor(lcd.CYAN)
  lcd.write(lcd.LASTX,lcd.LASTY," Clear screen")

  lcd.setcolor(lcd.YELLOW)
  lcd.write(60,dh*4+40,"R")
  lcd.setcolor(lcd.CYAN)
  lcd.write(lcd.LASTX,lcd.LASTY," Return, exit program")

  lcd.setcolor(lcd.YELLOW)
  lcd.write(60,dh*6+40,"2,4,6,8")
  lcd.setcolor(lcd.CYAN)
  lcd.write(lcd.LASTX,lcd.LASTY," draw size")

  paint.coYield(1)
  
  lcd.rect(0,20,dispx,dy-20,lcd.BLACK,lcd.BLACK)
  
  return dx, dy
end

-- Paint main loop
-- ---------------------
function paint.dopaint()
  local dispx, dispy, dx, dy, x, y, touch, lx, lx, first, drw, dodrw, color, lastc, lastr

  dx, dy = paint.paint_info()
  dispx, dispy = lcd.getscreensize()
  
  first = true
  drw = 1
  color = lcd.ORANGE
  lastc = dx*7
  r = 4
  lastr = dx
  
  while true do
	-- get touch status and coordinates
    touch, x, y = lcd.gettouch()
    if touch > 0 then
        dodrw = true
		
        if first and (y < 20) then
            -- === upper bar touched, color select ===
            lcd.rect(lastc,0,dx-2,18,color,color)
            if x > (dx*7) then
                color = lcd.ORANGE
                lastc = dx*7
            elseif x > (dx*6) then
                color = lcd.CYAN
                lastc = dx*6
            elseif x > (dx*5) then
                color = lcd.YELLOW
                lastc = dx*5
            elseif x > (dx*4) then
                color = lcd.BLUE
                lastc = dx*4
            elseif x > (dx*3) then
                color = lcd.GREEN
                lastc = dx*3
            elseif x > (dx*2) then
                color = lcd.RED
                lastc = dx*2
            elseif x > dx then
                color = lcd.WHITE
                lastc = dx
            elseif x > 1 then
                color = lcd.BLACK
                lastc = 0
            end
			lcd.rect(lastc+2,2,dx-6,14,lcd.WHITE,color)
			lcd.rect(lastc+3,3,dx-8,12,lcd.BLACK,color)
			-- wait for touch release
			paint.coYield(0)
            first = true

        elseif first and (y > dy) then
            -- === lower bar touched, size, r, erase shape select, return ===
            if x < (dx*5) then
                lcd.rect(lastr,dy,dx-2,dispy-dy,lcd.DARKGREY)
            end
            if x > (dx*7) then
                break
            elseif x > (dx*6) then
				-- clear drawing area
                lcd.rect(0,20,dispx,dy-20,lcd.BLACK,lcd.BLACK)
            elseif x > (dx*5) then
				-- change drawing shape
				drw = drw + 1
                if drw > 4 then
                    drw = 1
                end
				lcd.rect(dx*5,dy,dx-2,dispy-dy,lcd.DARKGREY, lcd.BLACK)
                if drw == 1 then
                    lcd.circle(dx*5+((dx-2)/2), dy+((dispy-dy)/2), (dispy-dy)/2-2, lcd.LIGHTGREY, lcd.LIGHTGREY)
                elseif drw == 3 then
                    lcd.rect(dx*5+6, dy+2, dx-14, dispy-dy-4, lcd.LIGHTGREY, lcd.LIGHTGREY)
                elseif drw == 2 then
                    lcd.circle(dx*5+((dx-2)/2), dy+((dispy-dy)/2), (dispy-dy)/2-2, lcd.YELLOW, lcd.DARKGREY)
                elseif drw == 4 then
                    lcd.rect(dx*5+6, dy+2, dx-14, dispy-dy-4, lcd.YELLOW, lcd.DARKGREY)
                end
			-- drawing size
            elseif x > (dx*4) then
                r = 10
                lastr = dx*4
            elseif x > (dx*3) then
                r = 8
                lastr = dx*3
            elseif x > (dx*2) then
                r = 6
                lastr = dx*2
            elseif x > dx then
                r = 4
                lastr = dx
            elseif x > 0 then
                r = 2
                lastr = 0
            end
            if x < (dx*5) then
                lcd.rect(lastr,dy,dx-2,dispy-dy,lcd.YELLOW)
            end
			-- wait for touch release
			paint.coYield(0)
            first = true

        elseif (x > r) and (y > (r+20)) and (y < (dy-r)) then
            -- === touch on drawing area, draw shape ===
            if first ~= true then
                if (math.abs(x-lx) > 5) or (math.abs(y-ly) > 5) then
                    dodrw = false
                end
            end
            
            if dodrw then
                if drw == 1 then
                    lcd.circle(x, y, r, color, color)
                elseif drw == 3 then
                    lcd.rect(x-r, y-r, r*2, r*2, color, color)
                elseif drw == 2 then
                    lcd.circle(x, y, r, lcd.DARKGREY, color)
                elseif drw == 4 then
                    lcd.rect(x-r, y-r, r*2, r*2, lcd.DARKGREY, color)
                end
            end
            -- save touched coordinates
            lx = x
            ly = y
            first = false
        end
    else
      first = true
	  paint.coYield(1)
    end
  end
  
  lcd.rect(0,dy,dispx,dispy-dy,lcd.YELLOW,lcd.BLACK)
  lcd.write(lcd.CENTER, dy+3, "FINISHED")
  
  timer.stop(paint.tmr)
  paint.waittouch = 0
  paint.running = false
end


-- check touch status and resume paint coroutine if needed
-- --------------------
function paint.tmr_cb()
	local stat, touch
	
	if paint.running then
		touch, _, _ = lcd.gettouch()
		if ((touch > 0) and (paint.waittouch == 1)) or ((touch == 0) and (paint.waittouch == 0)) then
			timer.pause(paint.tmr)
			stat = coroutine.resume(paint.coPaint)
			--[[
			if stat == false then
				if coroutine.status(paint.coPaint) == "dead" then
					paint.coPaint = coroutine.create(paint.dopaint)
					if type(paint.coPaint) == "thread" then
						coroutine.resume(paint.coPaint)
					end
				end
			end
			]]--
		end
	else
		timer.pause(paint.tmr)
	end
end

timer.changecb(paint.tmr, paint.tmr_cb)

-- -----------------------
function paint.run(orient)
	if paint.running then
		print("Already running")
	else
		timer.stop(paint.tmr)
		if orient ~= nil then
			paint.orient = orient
		end
		paint.waittouch = 0
		-- create a coroutine with paint.dopaint as the entry
		paint.coPaint = coroutine.create(paint.dopaint)
		paint.running = true
		timer.start(paint.tmr)
	end
end

