BOARD := 24g2usb_rp2350zero
KICAD := docker run --rm --user $(shell id -u):$(shell id -g) -v $(CURDIR)/..:/work -w /work/24g2usb_rp2350zero kicad/kicad:9.0-full
PCB := $(BOARD).kicad_pcb

.PHONY: all generate drc gerbers render zip clean

all: generate drc gerbers render zip

generate:
	docker run --rm --user $(shell id -u):$(shell id -g) -v $(CURDIR)/..:/work -w /work kicad/kicad:9.0-full \
		python3 generate.py --variant rp2350zero

drc:
	$(KICAD) kicad-cli pcb drc --severity-all --exit-code-violations \
		-o fab/drc-report.json --format json $(PCB)

gerbers:
	mkdir -p fab/gerbers
	$(KICAD) kicad-cli pcb export gerbers --layers \
		F.Cu,B.Cu,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
		-o fab/gerbers/ $(PCB)
	$(KICAD) kicad-cli pcb export drill -o fab/gerbers/ $(PCB)
	$(KICAD) kicad-cli pcb export pos -o fab/positions.csv --format csv --units mm $(PCB)
	$(KICAD) kicad-cli pcb export pdf -o fab/$(BOARD)-top-1to1.pdf --layers F.Cu,F.Silkscreen,F.Fab,Edge.Cuts \
		--black-and-white $(PCB)

render:
	mkdir -p fab
	$(KICAD) kicad-cli pcb export svg -o fab/$(BOARD)-top.svg --layers F.Cu,F.Silkscreen,F.Mask,F.Fab,Edge.Cuts $(PCB)
	$(KICAD) kicad-cli pcb export svg -o fab/$(BOARD)-bottom.svg --layers B.Cu,B.Silkscreen,B.Mask,Edge.Cuts $(PCB)
	$(KICAD) kicad-cli pcb render -o fab/$(BOARD)-3d-top.png --width 1600 --height 1200 \
		--side top --quality high --background opaque $(PCB)
	$(KICAD) kicad-cli pcb render -o fab/$(BOARD)-3d-bottom.png --width 1600 --height 1200 \
		--side bottom --quality high --background opaque $(PCB)
	$(KICAD) kicad-cli pcb render -o fab/$(BOARD)-3d-iso.png --width 1600 --height 1200 \
		--side top --rotate 20,0,-30 --perspective --quality high --background opaque $(PCB)

zip: gerbers
	cd fab/gerbers && python3 -c "import zipfile,os; z=zipfile.ZipFile('../$(BOARD)-gerbers.zip','w',zipfile.ZIP_DEFLATED); [z.write(f) for f in sorted(os.listdir('.'))]; z.close()"

clean:
	rm -rf fab/gerbers fab/*.zip fab/*.svg fab/*.png fab/*.pdf fab/drc-report.json fab/positions.csv fab/bom.csv
