## top ##################################
## 01/2022 Justin Rajendra
## Gang RBA 
## Server 

## server top #####################################
shinyServer(function(input,output,session) {
  options(warn=-1)
  session$onSessionEnded(function() { cat('\nAll done!\n') ; stopApp() })
  
  
  ## get data #########################################
  getROIs <- reactive({
    attach(input$fileSel)
    roi.temp <- as.data.frame(ps0)
    mod.terms <- terms
    detach(paste0('file:',input$fileSel),character.only=TRUE)
    rois.out <- list(roi.temp,mod.terms)
    return(rois.out)
  })   ## end getROIs
  
  ## update UI elements ###############################
  
  ## get the names of the ROIS
  observeEvent(input$fileSel,{
    temp.rois <- getROIs()
    temp.terms <- temp.rois[[2]]
    temp.rois <- temp.rois[[1]]
    
    rois.list <- as.list(names(temp.rois))
    names(rois.list) <- names(temp.rois)
    updateSelectInput(session,"roiSel",choices=rois.list,selected=rois.list)
    
    ## get the plot title from the last term
    last.term <- temp.terms[length(temp.terms)]
    if( last.term == 1 ){ last.term <- "Intercept" }
    updateTextInput(session,'x_label',value=last.term)
    # updateSliderInput(session,'colBarHeight',value=length(rois.list))
  })
  
  ## check plot dimensions in pixels
  output$cur_plot_dim <- renderText({
    if( input$autoWidth ){
      plot.w <- session$clientData[["output_bayesPlot_width"]]
    } else {
      plot.w <- input$plotWidth
    }
    # plot.h <- session$clientData[["output_bayesPlot_height"]]
    plot.h <- length(input$roiSel) * input$axesHeight
    paste(plot.w,"x",plot.h)
    # paste(input$dimension[1], input$dimension[2],
    #       input$dimension[3],input$dimension[4])
    # input$dimension[2]/input$dimension[1])
  })
  
  ## check plot dimensions in pixels
  output$out_plot_dim <- renderText({
    plot.w <- round(input$outputWidth / input$outputDPI,2)
    plot.h <- round(input$outputHeight / input$outputDPI,2)
    paste(plot.w,"x",plot.h)
  })
  
  ## match output dimensions if you change current views
  observeEvent(
    c(input$autoWidth,input$plotWidth,
      session$clientData[["output_bayesPlot_width"]]),
    {
      if( input$autoWidth ){
        plot.w <- session$clientData[["output_bayesPlot_width"]]
      } else {
        plot.w <- input$plotWidth
      }
      updateSliderInput(session,'outputWidth',value=plot.w)
    })
  
  observeEvent(input$plotRes,{
    updateSliderInput(session,'outputDPI',value=input$plotRes)
  })
  
  observe({
    updateSliderInput(session,'outputHeight',
                      value=length(input$roiSel) * input$axesHeight)
  })
  
  
  ## get stats ################################
  getStats <- reactive({
    
    ## get data remove unselected rois
    data <- getROIs()[[1]]
    
    validate(need(all(input$roiSel %in% names(data)),'     Need more rois!!!'))
    data <- subset(data,select=input$roiSel)
    
    data$X <- NULL
    nobj=dim(data)[1]
    # rename columns with ROI list
    # print(summary(data))
    rois <- names(data)
    colnames(data) <- rois
    data_stats <- data.frame(1:length(rois))
    
    # create ROI column instead of numerics to match  table above
    data_stats$ROI <- rois
    data_stats$P <- colSums(data > 0)/nobj
    data_stats$Pn <- ifelse(data_stats$P < .5, 1-data_stats$P, data_stats$P)
    
    # order type
    if( input$orderSel == 'P-plus' ){
      data_stats$mean <- colMeans(data)  
      data_stats <- data_stats[order(data_stats$mean),]
    } else if( input$orderSel == 'Original' ) {
      data_stats$mean <- apply(data, 2, quantile, .5) # # median: quantile(x, probs=.5)
    }
    
    data_trans <- as.data.frame(t(as.matrix(data)))
    # add two more columns
    data_trans <- tibble::rownames_to_column(data_trans, "ROI")
    data_trans$X <- 1:nrow(data_trans)
    
    # merge values & stats into one table by ROI
    data_merge <- merge(data_stats, data_trans, by="ROI")
    data_merge <- data_merge[order(data_merge$X),]
    #browser()
    # Transform data into long form: Melt dataframe by ROI
    data_long <- reshape2::melt(data_trans, id=c("ROI","X"))
    data_long <- data_long[order(data_long$X),]
    
    #clunky, but for now stats by ensuring orders are all the same and repeating each value nobj times. no success for alternatives. 
    data_long$mean <- rep(data_merge$mean, each=nobj)
    data_long$P <- rep(data_merge$P, each =nobj)
    data_long$Pn <- rep(data_merge$Pn, each =nobj)
    data_long$gray.vals <- rep(data_merge$gray.vals, each =nobj)
    
    # print(tail(data_long))
    # 
    # ## calculate some stats
    # roi.stats <- data.frame(matrix(ncol=0,nrow=length(rois.df)))
    # 
    # # create ROI column instead of numerics to match threat table above
    # roi.stats$ROI <- as.factor(names(rois.df))
    # roi.stats$mean <- colMeans(rois.df)  # median: quantile(x, probs=.5)
    # roi.stats$P <- colSums(rois.df > 0) / length(rois.df)
    # roi.stats$Pn <- ifelse(roi.stats$P < .5, 1-roi.stats$P, roi.stats$P)
    # 
    # # this will order the distributions correctly
    # roi.stats <- roi.stats[order(roi.stats$mean),]
    # 
    # 
    data.out <- list(data_stats,data_long)
    return(data.out)
  })   ## end getStats
  
  ## make plot ################################
  getPlot <- reactive({
    
    ### get the data ###############
    plot.list <- getStats()
    data_stats <- plot.list[[1]]
    data_long <- plot.list[[2]]
    rois <- data_stats$ROI
    
    x.values <- data_long$value
    y.values.order <- data_long$value
    distrib.fill <- data_long$P
    group <- data_long$ROI
    
    ## order type for y values
    if( input$orderSel == 'P-plus' ){
      y.plot <- as.numeric(reorder(data_long$ROI,y.values.order))
    } else if( input$orderSel == 'Original' ) {
      y.plot <- as.numeric(factor(data_long$ROI,levels=data_stats$ROI))
    }
    
    ### labels / titles ############
    legend.title <- input$colBar_title
    legend.title.size <- input$colBar_title_size
    legend.title.face <- input$colBar_face
    
    ## main title
    graph.title <- input$plotTitle
    title.size <- input$title_size
    title.face <- input$title_face
    
    ## x axis
    x.axis.labs <- input$x_label
    x.label.size <- input$x_label_size
    xlab.face <- input$xlab_face
    x.axis.size <- input$x_axis_size
    xtick.face <- input$x_axis_face
    
    ## y axes
    y.axis.labs <- data_stats$ROI
    sec.y.axis.labs <- sprintf('%.3f',data_stats$P)
    ROI.label.size <- input$ROI_label_size
    P.label.size <- input$P_label_size
    bar.label.size <- input$colBar_size
    
    ### colors #################
    gradient.colors <- c("blue","cyan","gray","gray","yellow","#C9182B")
    if( input$colPal == "Default" ){
      gradient.colors <- c("blue","cyan","gray","gray","yellow","#C9182B")
    } else {
      gradient.colors <- brewer.pal(input$numCols,input$colPal)
    } 
    if( input$revCols ){ gradient.colors <- rev(gradient.colors) }
    
    ### ranges #######
    if( input$x_range_custom ){
      x.range <- input$plotRange
    } else {
      x.range <- NULL
    }
    
    ### actual crazy plot ###############
    ggplot(
      data_long, 
      aes(x=x.values,y=y.plot,fill=distrib.fill,group=group)
    ) +
      
      ## color bar
      guides(
        fill=guide_colorbar(
          barwidth=1,barheight=input$colBarHeight,nbin=1000,
          frame.colour="black",frame.linewidth=1.5,ticks.colour="black",
          title.position="top",title.hjust=0,title.vjust=1)
      ) +
      
      ## fill for the legend (need to change this for other than P+)
      scale_fill_gradientn(
        colors=gradient.colors,limits=c(0,1),name=legend.title,
        breaks=c(0,0.05,0.1,0.5,0.9,0.95,1),expand=expansion(0),
        labels=c("0.00","0.05","0.10","0.50","0.90", "0.95","1.00")
      ) +
      
      ## divide into 2 quantiles (median NOT MEAN!!!)
      stat_density_ridges(
        quantile_lines=TRUE,quantiles=2,size=.6,alpha=.8,scale=2,color="black"
      ) +
      
      ## zero line
      geom_vline(
        xintercept=0,linetype="solid",alpha=1,size=1,color="green3"
      ) +
      
      ## setup both y axes
      scale_y_continuous(
        breaks=1:length(rois),labels=y.axis.labs,expand=c(0,0.2),
        sec.axis=sec_axis(~.,breaks=1:length(rois),labels=sec.y.axis.labs)
      ) +
      
      ## configure the ridgeline plot
      theme_ridges(font_size=bar.label.size,grid=TRUE,
                   center_axis_labels=TRUE) +
      
      ## title
      ggtitle(graph.title) +
      
      ## decorations
      theme(
        plot.title=element_text(hjust=0.5,vjust=-0.5,
                                size=title.size,face=title.face),
        
        ## y axes
        axis.text.y.left=element_text(size=ROI.label.size),
        axis.text.y.right=element_text(size=P.label.size),
        
        ## x axis
        axis.text.x=element_text(size=x.axis.size,face=xtick.face),
        axis.title=element_text(size=x.label.size,face=xlab.face),
        
        ## label above color bar
        legend.title.align=0,
        legend.title=element_text(size=legend.title.size,
                                  face=legend.title.face),
        legend.justification="top",
        
        ## background fill
        panel.background=element_rect(fill='white'),
        plot.background=element_rect(fill='white')
      ) +
      
      ## axis labels
      labs(
        x=x.axis.labs,
        y=NULL
      ) +
      
      ## x axis ticks and range
      scale_x_continuous(labels=waiver(),limits=x.range)
    
  })   ## end make plot
  
  
  ## output plot ###############################
  ## need observe here to get the variables for height
  observe({
    validate(need(length(input$roiSel) > 0,'     Need more rois!!!'))
    output$bayesPlot <- renderPlot({
      getPlot() }, 
      res=input$plotRes,
      height=length(input$roiSel) * input$axesHeight,
      width=({
        if( input$autoWidth ){
          p.width <- 'auto'
        } else {
          p.width <- input$plotWidth
        }
        p.width
      })
    )
  })
  
  # output$gangPlot <- renderImage({
  #   outfile <- tempfile(fileext = '.png')
  #   ggsave(outfile,plot=getPlot(),
  #          width=input$outputWidth,height=input$outputHeight,
  #          units='in',dpi=input$outputDPI)
  #   # Return a list containing the filename
  #   list(src = outfile,
  #        contentType = 'image/png',
  #        alt = "This is alternate text")
  # }, deleteFile = TRUE)
  
  ## download plot #################
  
  ## plot warnings
  
  observeEvent(
    c(input$outputWidth,input$outputDPI),
    {
      if( (input$outputWidth / input$outputDPI) > 50 ){
        showNotification("Output file width > 50 inches!!!",
                         type='error',duration=NULL)
      }
    })
  observeEvent(
    c(input$outputHeight,input$outputDPI),
    {
      if( (input$outputHeight / input$outputDPI) > 50 ){
        showNotification("Output file height > 50 inches!!!",
                         type='error',duration=NULL)
      }
    })
  
  output$downloadPlot <- downloadHandler(
    filename = function(){
      paste0(file_path_sans_ext(input$fileSel),'_plot.',input$outputFormat)
    },
    content = function(file) {
      
      # validate(need((input$outputWidth / input$outputDPI) > 50,
      #               '     Output file width is greater than 50 inches!!!'))   
      # validate(need(input$outputHeight / input$outputDPI > 50,
      #               '     Output file height is greater than 50 inches!!!'))
      
      ggsave(file,plot=getPlot(),
             width=input$outputWidth,height=input$outputHeight,
             units='px',
             dpi=input$outputDPI
      )
    })
  
  ## output stats table #######################
  output$statsTable <- DT::renderDataTable({
    
    ## get data remove unselected rois
    data <- getROIs()[[1]]
    
    validate(need(all(input$roiSel %in% names(data)),'     Need more rois!!!'))
    data <- subset(data,select=input$roiSel)
    
    data$X <- NULL
    nobj=dim(data)[1]
    # rename columns with ROI list
    # print(summary(data))
    rois <- names(data)
    colnames(data) <- rois
    data_stats <- data.frame(1:length(rois))
    
    # create ROI column instead of numerics to match  table above
    data_stats$ROI <- rois
    data_stats$P <- colSums(data > 0)/nobj
    data_stats$Pn <- ifelse(data_stats$P < .5, 1-data_stats$P, data_stats$P)
    
    ## calculate more stats
    data_stats$mean <- colMeans(data)  
    data_stats$median <- apply(data,2, quantile,.5) # # median: quantile(x, probs=.5)
    
    data_stats$median2 <- data_stats$median3 <- data_stats$median4 <- data_stats$median
    data_stats$median22 <- data_stats$median23 <- data_stats$median24 <- data_stats$median233 <- data_stats$median2
    
    # # order type
    # if( input$orderSel == 'P-plus' ){
    #   data_stats <- data_stats[order(data_stats$mean),]
    # } else if( input$orderSel == 'Original' ) {
    #   data_stats <- data_stats[order(data_stats$median),]
    # }
    
    ## remove first counting variable
    data_stats <- data_stats[2:length(data_stats)]
    # data_stats <- datatable(as.data.frame(data_stats))
    # data_stats <- formatRound(table=data_stats,digits=4)
    
    brks <- quantile(data_stats$Pn, probs = seq(.05, .95, .05), na.rm = TRUE)
    
    print(brks)
    
    gradient.colors <- c("blue","cyan","gray","gray","yellow","#C9182B")
    if( input$colPal == "Default" ){
      gradient.colors <- c("blue","cyan","gray","gray","yellow","#C9182B")
    } else {
      gradient.colors <- brewer.pal(input$numCols,input$colPal)
    } 
    if( input$revCols ){ gradient.colors <- rev(gradient.colors) }
    # 19 values
    
    datatable(
      data_stats,
      extensions="FixedColumns",
      options=list(pageLength=100,scrollX=TRUE,
                   fixedColumns = list(leftColumns = 2))) %>% 
      
      formatSignif(columns=c(3:length(data_stats)),digits=4) 
    # %>% 
      
      # 
      # formatStyle(
      #   'Pn',
      #   background = styleInterval(data_stats$Pn, gradient.colors)
      # )
    
    
      # formatStyle(backgroundColor = styleInterval(brks, gradient.colors))
    
  }
  
  
  # server = FALSE,selection = 'single')
  )
  
  
})   ## end server ###########################



# ## variable tables #####################
# output$var_tab <- renderTable(colnames=FALSE,{rate.names})
# 
# output$bad_vars <- renderTable(colnames=FALSE,{
#   
#   ## get the text and split the vars
#   model.vars <- unlist(tstrsplit(input$model_in,'[*+-/^]'))
#   # model.vars <- gsub("[[:space:]]", "",model.vars)
#   bad.vars <- setdiff(as.character(model.vars),rate.names)
#   bad.vars
# })
# 
# output$meta_tab <- renderPrint({
#   validate(need(input$model_in,"Please specify model!"))
#   model.vars <- unlist(tstrsplit(input$model_in,'[*+-/^]'))
#   bad.vars <- model.vars[ ! (model.vars %in% rate.names) ]
#   
#   if( length(bad.vars) > 0 ){ return("Check variable names!") }
#   mod.df <- getOne()
#   meta.calc <- try(parse(text=paste0("with(mod.df,",input$model_in,")")),
#                    silent=TRUE)
#   meta.out <- try(eval(meta.calc),silent=TRUE)
#   summary(meta.out)
# })